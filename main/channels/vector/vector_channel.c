/* Vector 机器人通道实现 — 含音频缓冲 + 能量 VAD + ASR 管道 */

#include "channels/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "bus/message_bus.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_config.h"
#include "voice/voice_channel.h"
#include "cJSON.h"

static const char *TAG = "vector";

/* VAD 参数 */
#define VAD_SAMPLE_RATE       16000
#define VAD_CHUNK_SAMPLES     1920    /* 120ms @ 16kHz */
#define VAD_SPEECH_THRESHOLD  400.0   /* RMS 阈值 */
#define VAD_SILENCE_TIMEOUT   25      /* 连续静音帧数 (~120ms * 25 = 3s) */
#define VAD_MAX_SAMPLES       (VAD_SAMPLE_RATE * 30)  /* 30 秒上限 */

typedef struct {
    pthread_mutex_t mutex;
    mcp_client_t   *mcp;
    bool            audio_subscribed;
    bool            running;
    char            bin_path[512];

    /* 音频缓冲 (PCM 16-bit signed LE, 16kHz mono) */
    int16_t *pcm_buf;
    size_t   pcm_cap;
    size_t   pcm_len;
    bool     speaking;
    int      silence_frames;
    uint64_t last_chunk_ts;
    bool     playing;
    uint64_t mute_ts;
    uint16_t mic_direction;
    uint16_t mic_selected_dir;
    int16_t  mic_confidence;
    uint32_t prox_distance_mm;
    bool     prox_found_object;
    bool     prox_unobstructed;
    bool     cliff_detected;
    uint32_t robot_status;
    float    head_angle_deg;
} vector_session_t;

static vector_session_t *s = NULL;

/* 计算 RMS (root mean square) */
static double pcm_rms(const int16_t *samples, size_t count)
{
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += (double)samples[i] * (double)samples[i];
    }
    return sqrt(sum / (double)count);
}

/* 确保缓冲区容量 */
static bool pcm_buf_ensure(size_t need)
{
    if (need <= s->pcm_cap) return true;
    size_t new_cap = s->pcm_cap ? s->pcm_cap * 2 : VAD_CHUNK_SAMPLES * 32;
    while (new_cap < need) new_cap *= 2;
    if (new_cap > VAD_MAX_SAMPLES * 2) return false;
    int16_t *p = realloc(s->pcm_buf, new_cap * sizeof(int16_t));
    if (!p) return false;
    s->pcm_buf = p;
    s->pcm_cap = new_cap;
    return true;
}

static void pcm_buf_flush_to_asr(void)
{
    /* Copy PCM under lock, release before slow ASR call */
    pthread_mutex_lock(&s->mutex);
    if (s->pcm_len < 1600) {
        s->pcm_len = 0;
        s->speaking = false;
        s->silence_frames = 0;
        pthread_mutex_unlock(&s->mutex);
        return;
    }

    size_t n_samples = s->pcm_len;
    size_t pcm_bytes = n_samples * sizeof(int16_t);
    int16_t *pcm_copy = malloc(pcm_bytes);
    if (!pcm_copy) {
        s->pcm_len = 0;
        s->speaking = false;
        s->silence_frames = 0;
        pthread_mutex_unlock(&s->mutex);
        return;
    }
    memcpy(pcm_copy, s->pcm_buf, pcm_bytes);
    s->pcm_len = 0;
    s->speaking = false;
    s->silence_frames = 0;

    /* Shrink buffer if it grew too large */
    if (s->pcm_cap > VAD_CHUNK_SAMPLES * 64) {
        free(s->pcm_buf);
        s->pcm_buf = NULL;
        s->pcm_cap = 0;
    }

    pthread_mutex_unlock(&s->mutex);

    DAIMA_LOGI(TAG, "ASR flush: %zu samples (%.1f sec)", n_samples,
               (double)n_samples / VAD_SAMPLE_RATE);

    /* Build WAV header + send to ASR (no lock held) */
    size_t wav_size = 44 + pcm_bytes;
    uint8_t *wav_buf = malloc(wav_size);
    if (!wav_buf) {
        DAIMA_LOGW(TAG, "ASR: malloc failed");
        free(pcm_copy);
        return;
    }

    memcpy(wav_buf,     "RIFF", 4);
    *(uint32_t *)(wav_buf + 4)  = (uint32_t)(wav_size - 8);
    memcpy(wav_buf + 8,  "WAVE", 4);
    memcpy(wav_buf + 12, "fmt ", 4);
    *(uint32_t *)(wav_buf + 16) = 16;
    *(uint16_t *)(wav_buf + 20) = 1;
    *(uint16_t *)(wav_buf + 22) = 1;
    *(uint32_t *)(wav_buf + 24) = VAD_SAMPLE_RATE;
    *(uint32_t *)(wav_buf + 28) = VAD_SAMPLE_RATE * 2;
    *(uint16_t *)(wav_buf + 32) = 2;
    *(uint16_t *)(wav_buf + 34) = 16;
    memcpy(wav_buf + 36, "data", 4);
    *(uint32_t *)(wav_buf + 40) = (uint32_t)pcm_bytes;
    memcpy(wav_buf + 44, pcm_copy, pcm_bytes);
    free(pcm_copy);

    daima_err_t err = voice_channel_handle_audio(
        DAIMA_CHAN_VECTOR, wav_buf, wav_size,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    free(wav_buf);

    if (err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "ASR failed: %s", daima_err_to_name(err));
    }
}

static void on_audio_done(const mcp_audio_direction_t *dir, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&s->mutex);
    if (dir) {
        s->mic_direction = dir->direction;
        s->mic_selected_dir = dir->selectedDirection;
        s->mic_confidence = dir->confidence;
        s->prox_distance_mm = dir->prox_distance_mm;
        s->prox_found_object = dir->prox_found_object;
        s->prox_unobstructed = dir->prox_unobstructed;
        s->cliff_detected = dir->cliff_detected;
        s->robot_status = dir->robot_status;
        s->head_angle_deg = dir->head_angle_deg;
    }
    if (s->pcm_len >= 1600) {
        DAIMA_LOGI(TAG, "AudioDone: flushing %zu samples (dir=%d prox=%umm cliff=%d)",
                   s->pcm_len, s->mic_direction, s->prox_distance_mm, s->cliff_detected);
        pthread_mutex_unlock(&s->mutex);
        pcm_buf_flush_to_asr();
        return;
    }
    s->pcm_len = 0;
    s->speaking = false;
    s->silence_frames = 0;
    pthread_mutex_unlock(&s->mutex);
}

static void on_vector_audio(const uint8_t *pcm, size_t len, uint64_t timestamp, void *user_data)
{
    (void)user_data;
    (void)timestamp;

    size_t num_samples = len / sizeof(int16_t);
    if (num_samples == 0) return;

    pthread_mutex_lock(&s->mutex);
    bool playing = s->playing;
    pthread_mutex_unlock(&s->mutex);
    if (playing) return;

    const int16_t *samples = (const int16_t *)pcm;
    double rms = pcm_rms(samples, num_samples);
    bool is_speech = (rms > VAD_SPEECH_THRESHOLD);

    pthread_mutex_lock(&s->mutex);
    s->last_chunk_ts = (uint64_t)time(NULL);

    if (is_speech) {
        if (!s->speaking) {
            DAIMA_LOGI(TAG, "VAD: speech start (rms=%.0f)", rms);
        }
        s->speaking = true;
        s->silence_frames = 0;
    } else if (s->speaking) {
        s->silence_frames++;
        if (s->silence_frames >= VAD_SILENCE_TIMEOUT) {
            DAIMA_LOGI(TAG, "VAD: speech end (silence=%d frames)", s->silence_frames);
            pthread_mutex_unlock(&s->mutex);
            pcm_buf_flush_to_asr();
            return;
        }
    }

    size_t new_len = s->pcm_len + num_samples;
    if (!pcm_buf_ensure(new_len)) {
        DAIMA_LOGW(TAG, "PCM buffer full, flushing");
        pthread_mutex_unlock(&s->mutex);
        pcm_buf_flush_to_asr();
        return;
    }
    memcpy(s->pcm_buf + s->pcm_len, samples, len);
    s->pcm_len = new_len;

    if (s->pcm_len >= (size_t)VAD_MAX_SAMPLES) {
        DAIMA_LOGI(TAG, "VAD: max duration reached, flushing");
        pthread_mutex_unlock(&s->mutex);
        pcm_buf_flush_to_asr();
        return;
    }
    pthread_mutex_unlock(&s->mutex);
}

/* 后台任务：初始化 MCP 连接（可能耗时，不阻塞主线程） */
static void vector_connect_task(void *arg)
{
    (void)arg;

    /* Wait for Gatewway to start (vic-cloud takes ~3s after systemd) */
    DAIMA_LOGI(TAG, "Waiting for Gateway...");
    for (int i = 0; i < 20 && s->running; i++) {
        sleep(1);
        if (s->mcp) break; /* already connected in a previous attempt */
    }

    const char *robot_addr = getenv("DAIMA_ROBOT_ADDR");
    const char *token_file = getenv("DAIMA_TOKEN_FILE");

    /* mcp_client_launch 可能阻塞数秒等机器人连接 */
    mcp_client_t *mcp = mcp_client_launch(s->bin_path, robot_addr, token_file);
    pthread_mutex_lock(&s->mutex);
    s->mcp = mcp;
    pthread_mutex_unlock(&s->mutex);

    if (!mcp) {
        DAIMA_LOGW(TAG, "Failed to launch robot-mcp (retrying in 30s...)");
        int retry = 0;
        while (s->running && retry < 100) {
            sleep(30);
            if (!s->running) return;
            mcp = mcp_client_launch(s->bin_path, robot_addr, token_file);
            pthread_mutex_lock(&s->mutex);
            s->mcp = mcp;
            pthread_mutex_unlock(&s->mutex);
            if (mcp) break;
            retry++;
        }
        if (!mcp) return;
    }

    /* 注册音频回调 - 含缓冲 + VAD */
    mcp_client_set_audio_callback(mcp, on_vector_audio, NULL);

    /* 注册 AudioDone 回调 — 机器人检测到说话结束，直接 flush */
    mcp_client_set_audio_done_callback(mcp, on_audio_done, NULL);

    /* 订阅音频流 */
    daima_err_t err = mcp_client_subscribe_audio(mcp);
    if (err == DAIMA_OK) {
        s->audio_subscribed = true;
        DAIMA_LOGI(TAG, "Audio subscribed (VAD: threshold=%.0f RMS, silence=%d frames)",
                   VAD_SPEECH_THRESHOLD, VAD_SILENCE_TIMEOUT);
    } else {
        DAIMA_LOGW(TAG, "Audio subscribe failed");
    }

    /* Set volume once after connection */
    char vol_resp[64];
    mcp_client_call_tool(mcp, "robot_set_volume", "{\"level\":4}", vol_resp, sizeof(vol_resp));
    DAIMA_LOGI(TAG, "Volume set: %s", vol_resp);

    DAIMA_LOGI(TAG, "Vector channel connected");
}

/* 后台任务：轮询 MCP 子进程的音频通知 */
static void vector_poll_task(void *arg)
{
    (void)arg;
    DAIMA_LOGI(TAG, "Poll task started");
    while (s->running) {
        if (s->mcp) {
            int n = mcp_client_poll(s->mcp);
            if (n > 0) {
                DAIMA_LOGD(TAG, "Processed %d MCP messages", n);
            }
        }
        /* 超时保护：如果 5s 内没收到新音频 chunk，强制 flush */
        if (s->speaking && s->pcm_len > 0 &&
            s->last_chunk_ts > 0) {
            uint64_t now = (uint64_t)time(NULL);
            if (now >= s->last_chunk_ts + 5) {
                DAIMA_LOGI(TAG, "VAD: timeout flush (no new chunks for 5s)");
                pcm_buf_flush_to_asr();
            }
        }
        /* 超时 unmute: 如果 muted >10s 还没有回复，自动恢复 mic */
        if (s->playing && s->mute_ts > 0) {
            uint64_t now = (uint64_t)time(NULL);
            if (now >= s->mute_ts + 10) {
                DAIMA_LOGI(TAG, "Mic: auto-unmute (timeout 10s)");
                s->playing = false;
            }
        }
        usleep(50000); /* 50ms poll interval */
    }
    DAIMA_LOGI(TAG, "Poll task stopped");
}

daima_err_t vector_channel_init(void)
{
    DAIMA_LOGI(TAG, "Initializing vector channel");

    if (!s) {
        s = calloc(1, sizeof(vector_session_t));
        if (!s) return DAIMA_ERR_NO_MEM;
        pthread_mutex_init(&s->mutex, NULL);
    }

    const char *custom = getenv("DAIMA_MCP_BIN");
    if (custom && custom[0]) {
        snprintf(s->bin_path, sizeof(s->bin_path), "%s", custom);
    } else {
        snprintf(s->bin_path, sizeof(s->bin_path), "%s", MCP_BIN_DEFAULT);
    }

    DAIMA_LOGI(TAG, "MCP binary: %s", s->bin_path);
    return DAIMA_OK;
}

daima_err_t vector_channel_start(void)
{
    DAIMA_LOGI(TAG, "Vector channel starting (async connect)...");

    /* 启动后台连接任务（不阻塞） */
    s->running = true;
    daima_task_create(vector_connect_task, "vector_conn",
                      MCP_POLL_STACK, NULL, MCP_POLL_PRIO, NULL);

    /* 启动后台轮询任务 */
    daima_task_create(vector_poll_task, "vector_poll",
                      MCP_POLL_STACK, NULL, MCP_POLL_PRIO, NULL);

    return DAIMA_OK;
}

daima_err_t vector_channel_send_reply(const char *chat_id, const char *text)
{
    (void)chat_id;
    /* TTS is handled by the voice channel (BigModel → PCM → socket).
     * robot_say_text (built-in TTS) is not registered in robot-mcp. */
    DAIMA_LOGD(TAG, "Reply text: %.60s", text ? text : "");
    return DAIMA_OK;
}

daima_err_t vector_channel_play_pcm(const unsigned char *pcm, size_t pcm_len, uint32_t sample_rate, uint32_t seq, const char *label)
{
    (void)sample_rate;
    if (!pcm || pcm_len == 0) return DAIMA_ERR_INVALID_ARG;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        DAIMA_LOGW(TAG, "PlayPCM: socket failed");
        return DAIMA_FAIL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/daima_spk.sock");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        DAIMA_LOGW(TAG, "PlayPCM: connect failed (robot-mcp not ready?)");
        close(fd);
        return DAIMA_FAIL;
    }

    /* Write: seq(4) + text_len(2) + text + pcm */
    uint32_t seq_le = seq;
    uint16_t label_len = label ? (uint16_t)strlen(label) : 0;
    if (write(fd, &seq_le, 4) != 4 ||
        write(fd, &label_len, 2) != 2 ||
        (label_len > 0 && write(fd, label, label_len) != (ssize_t)label_len) ||
        write(fd, pcm, pcm_len) != (ssize_t)pcm_len) {
        DAIMA_LOGW(TAG, "PlayPCM: write failed");
        close(fd);
        return DAIMA_FAIL;
    }

    close(fd);
    return DAIMA_OK;
}

mcp_client_t *vector_channel_get_mcp(void)
{
    pthread_mutex_lock(&s->mutex);
    mcp_client_t *m = s->mcp;
    pthread_mutex_unlock(&s->mutex);
    return m;
}

void vector_channel_mute_mic(bool mute)
{
    pthread_mutex_lock(&s->mutex);
    s->playing = mute;
    if (mute) {
        s->pcm_len = 0;
        s->speaking = false;
        s->silence_frames = 0;
        s->mute_ts = (uint64_t)time(NULL);
    }
    pthread_mutex_unlock(&s->mutex);
}

uint16_t vector_channel_get_mic_direction(void)
{
    if (!s) return 0xFF;
    uint16_t dir;
    pthread_mutex_lock(&s->mutex);
    dir = s->mic_direction;
    pthread_mutex_unlock(&s->mutex);
    return dir;
}

void vector_channel_get_sensor_snapshot(vector_sensor_snapshot_t *out)
{
    if (!s || !out) return;
    pthread_mutex_lock(&s->mutex);
    out->prox_distance_mm  = s->prox_distance_mm;
    out->prox_found_object = s->prox_found_object;
    out->prox_unobstructed = s->prox_unobstructed;
    out->cliff_detected    = s->cliff_detected;
    out->robot_status      = s->robot_status;
    out->head_angle_deg    = s->head_angle_deg;
    pthread_mutex_unlock(&s->mutex);
}
