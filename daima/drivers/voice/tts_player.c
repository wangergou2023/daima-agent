/* tts_player — 语音播放管线: 分句 → 并行 TTS → socket → robot-mcp
 *   多句并行: N个线程同时调BigModel，先完成的先写socket
 *   robot-mcp的队列保证播放顺序 */

#include "drivers/voice/tts_player.h"
#include "drivers/voice/voice_channel.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "linux/printk.h"
#include "drivers/channel/vector/vector_channel.h"
#include "linux/slab.h"

static const char *TAG = "tts_player";

static uint32_t g_tts_seq = 0;

#define LOWPASS_CUTOFF  4000.0
#define GAIN_FACTOR     1.0
#define PCM_TARGET_RATE 16000
#define SOFT_LIMIT      24
#define MAX_SENTENCES   3

typedef struct {
    char  text[512];
    unsigned char *pcm;
    size_t pcm_len;
    long  tts_ms;
} sentence_job_t;

static void apply_gain(int16_t *samples, size_t count, double factor)
{
    for (size_t i = 0; i < count; i++) {
        double s = (double)samples[i] * factor;
        samples[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}

static void apply_lowpass(int16_t *samples, size_t count, double cutoff, int sample_rate)
{
    if (count < 2) return;
    double rc = 1.0 / (2.0 * 3.141592653589793 * cutoff);
    double dt = 1.0 / sample_rate;
    double alpha = dt / (rc + dt);
    double prev = (double)samples[0];
    for (size_t i = 1; i < count; i++) {
        double current = (double)samples[i];
        double filtered = alpha * current + (1.0 - alpha) * prev;
        samples[i] = (int16_t)(filtered > 32767 ? 32767 : (filtered < -32768 ? -32768 : filtered));
        prev = filtered;
    }
}

static size_t rune_len(const char *s)
{
    size_t n = 0;
    while (*s) { if ((*s & 0xC0) != 0x80) n++; s++; }
    return n;
}

static void *tts_thread(void *arg)
{
    sentence_job_t *job = (sentence_job_t *)arg;
    struct timespec ts, te;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t rate = PCM_TARGET_RATE;
    daima_err_t err = voice_channel_get_tts_pcm(job->text, &job->pcm, &job->pcm_len, &rate);
    clock_gettime(CLOCK_MONOTONIC, &te);
    job->tts_ms = (te.tv_sec - ts.tv_sec) * 1000 + (te.tv_nsec - ts.tv_nsec) / 1000000;
    if (err == DAIMA_OK && job->pcm && job->pcm_len > 0) {
        size_t count = job->pcm_len / sizeof(int16_t);
        apply_gain((int16_t *)job->pcm, count, GAIN_FACTOR);
        apply_lowpass((int16_t *)job->pcm, count, LOWPASS_CUTOFF, (int)rate);
    }
    return NULL;
}

daima_err_t tts_player_speak(const char *text)
{
    if (!text || !*text) return DAIMA_ERR_INVALID_ARG;

    DAIMA_LOGI(TAG, "Speak start: len=%zu text=[%s]", strlen(text), text);

    vector_channel_mute_mic(true);

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    float total_audio_sec = 0.0f;

    size_t text_len = strlen(text);
    char *buf = kmalloc(text_len + 1, GFP_KERNEL);
    if (!buf) return DAIMA_ERR_NO_MEM;
    memcpy(buf, text, text_len + 1);

    /* Collect sentences */
    sentence_job_t jobs[MAX_SENTENCES] = {{{0}}};
    int n = 0;

    if (rune_len(text) <= SOFT_LIMIT) {
        /* Short text: one TTS, no splitting */
        snprintf(jobs[0].text, sizeof(jobs[0].text), "%s", text);
        n = 1;
    } else {
        /* Split by sentence-ending punctuation */
        static const char *end_punct[][2] = {
            {"。", "\n"}, {"！", "\n"}, {"？", "\n"},
            {".", "\n"}, {"!", "\n"}, {"?", "\n"},
            {NULL, NULL},
        };
        for (int i = 0; end_punct[i][0]; i++) {
            const char *needle = end_punct[i][0];
            size_t nlen = strlen(needle);
            char *pos = buf;
            while ((pos = strstr(pos, needle)) != NULL) { memset(pos, '\n', nlen); pos += nlen; }
        }

        /* First pass: collect all raw sentences */
        char *raw[64]; int raw_n = 0;
        char *saveptr = NULL;
        char *sent = strtok_r(buf, "\n", &saveptr);
        while (sent && raw_n < 64) {
            while (*sent == ' ' || *sent == '\t' || *sent == '\n') sent++;
            size_t rlen = rune_len(sent);
            if (*sent && rlen >= 3) raw[raw_n++] = sent;
            sent = strtok_r(NULL, "\n", &saveptr);
        }

        /* Merge short sentences (<10 runes) with neighbors, then cap at MAX_SENTENCES */
        struct { char text[512]; } groups[64];
        int g_n = 0;
        {
            char mbuf[4096] = {0};
            int moff = 0;
            for (int i = 0; i < raw_n; i++) {
                moff += snprintf(mbuf + moff, sizeof(mbuf) - moff,
                                 "%s%s", moff > 0 ? "。" : "", raw[i]);
                size_t rlen = rune_len(mbuf);
                if (rlen <= 10 && i + 1 < raw_n) continue;
                snprintf(groups[g_n].text, sizeof(groups[g_n].text), "%s", mbuf);
                g_n++;
                mbuf[0] = '\0';
                moff = 0;
            }
            if (moff > 0) {
                snprintf(groups[g_n].text, sizeof(groups[g_n].text), "%s", mbuf);
                g_n++;
            }
        }

        /* If >MAX_SENTENCES groups, merge evenly: keep merging shortest neighbors */
        while (g_n > MAX_SENTENCES) {
            /* Find two consecutive groups with smallest combined length */
            int best = 0;
            size_t best_len = (size_t)-1;
            for (int i = 0; i < g_n - 1; i++) {
                size_t combined = rune_len(groups[i].text) + rune_len(groups[i+1].text);
                if (combined < best_len) { best = i; best_len = combined; }
            }
            /* Merge best and best+1 */
            char merged[1024];
            snprintf(merged, sizeof(merged), "%s。%s", groups[best].text, groups[best+1].text);
            snprintf(groups[best].text, sizeof(groups[0].text), "%s", merged);
            /* Shift remaining left */
            for (int i = best + 1; i < g_n - 1; i++) {
                snprintf(groups[i].text, sizeof(groups[0].text), "%s", groups[i+1].text);
            }
            g_n--;
        }

        for (int i = 0; i < g_n; i++) {
            snprintf(jobs[i].text, sizeof(jobs[i].text), "%s", groups[i].text);
        }
        n = g_n;
    }
    kfree(buf);

    DAIMA_LOGD(TAG, "Collected %d sentences", n);
    if (n == 0) {
        DAIMA_LOGI(TAG, "Speak done: no valid sentences");
        vector_channel_mute_mic(false);
        return DAIMA_OK;
    }

    /* Serial TTS — BigModel concurrency causes corrupted audio */
    for (int i = 0; i < n; i++) {
        DAIMA_LOGI(TAG, "  TTS[%d/%d] start: [%s]", i+1, n, jobs[i].text);
        pthread_t th;
        pthread_create(&th, NULL, tts_thread, &jobs[i]);
        pthread_join(th, NULL);
        if (jobs[i].pcm && jobs[i].pcm_len > 0) {
            DAIMA_LOGI(TAG, "  TTS[%d/%d] done: TTS=%ldms PCM=%zuKB", i+1, n, jobs[i].tts_ms, jobs[i].pcm_len/1024);
            total_audio_sec += (float)jobs[i].pcm_len / (PCM_TARGET_RATE * 2.0f);
            struct timespec ts, te;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            vector_channel_play_pcm(jobs[i].pcm, jobs[i].pcm_len, PCM_TARGET_RATE, g_tts_seq++, jobs[i].text);
            clock_gettime(CLOCK_MONOTONIC, &te);
            long ms = (te.tv_sec - ts.tv_sec) * 1000 + (te.tv_nsec - ts.tv_nsec) / 1000000;
            float audio = (float)jobs[i].pcm_len / (PCM_TARGET_RATE * 2.0f);
            DAIMA_LOGD(TAG, "  [%d/%d] TTS %ldms + push %ldms (audio %.1fs)", i+1, n, jobs[i].tts_ms, ms, audio);
            kfree(jobs[i].pcm);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    long total = (t_end.tv_sec - t_start.tv_sec) * 1000 + (t_end.tv_nsec - t_start.tv_nsec) / 1000000;
    DAIMA_LOGI(TAG, "Speak done: %ldms total (%d sentences), audio=%.1fs", total, n, total_audio_sec);

    if (total_audio_sec > 0.5f) {
        DAIMA_LOGI(TAG, "Waiting %.1fs for playback before unmute", total_audio_sec);
        struct timespec req = { (time_t)total_audio_sec, (long)((total_audio_sec - (int)total_audio_sec) * 1e9) };
        nanosleep(&req, NULL);
    }

    vector_channel_mute_mic(false);
    return DAIMA_OK;
}
