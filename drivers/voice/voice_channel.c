/* 语音通道实现：ASR -> LLM -> TTS。 */

#include "drivers/voice/voice_channel.h"
#include "runtime.h"
#include "tls.h"
#include "bus.h"
#include "http.h"
#include "proxy.h"
#include "paths.h"
#include "autoconf.h"
#include "env.h"
#include "drivers/audio/audio_io.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>
#include "linux/printk.h"
#include "cjson.h"
#include "linux/slab.h"
/* BigModel API endpoints */
static const char *BIGMODEL_ASR_URL = "https://open.bigmodel.cn/api/paas/v4/audio/transcriptions";
static const char *BIGMODEL_TTS_URL = "https://open.bigmodel.cn/api/paas/v4/audio/speech";

/* 默认模型 */
static const char *DEFAULT_ASR_MODEL = "glm-asr-2512";
static const char *DEFAULT_TTS_MODEL = "glm-tts";
static const char *DEFAULT_VOICE = "tongtong";
static const char *DEFAULT_TTS_FORMAT = "wav";

/* API key */
static char s_bigmodel_key[128] = {0};

static pthread_once_t s_curl_once = PTHREAD_ONCE_INIT;

static void curl_global_init_once(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

typedef struct {
    char *data;
    size_t len;
} buf_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    buf_t *buf = (buf_t *)userp;
    char *ptr = realloc(buf->data, buf->len + realsize + 1);
    if (!ptr) return 0;
    buf->data = ptr;
    memcpy(&(buf->data[buf->len]), contents, realsize);
    buf->len += realsize;
    buf->data[buf->len] = '\0';
    return realsize;
}

static void apply_proxy(CURL *curl)
{
    if (!http_proxy_is_enabled()) return;

    const char *host = http_proxy_host();
    uint16_t port = http_proxy_port();
    const char *type = http_proxy_type();
    if (!host || !host[0] || port == 0) return;

    char proxy[256];
    const char *scheme = "http";
    if (type && strcmp(type, "socks5") == 0) {
        scheme = "socks5h";
    }
    snprintf(proxy, sizeof(proxy), "%s://%s:%u", scheme, host, port);

    curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
    if (type && strcmp(type, "socks5") == 0) {
#ifdef CURLPROXY_SOCKS5_HOSTNAME
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
#else
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
#endif
    } else {
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
    }
}

static void apply_ca_cert(CURL *curl)
{
    host_tls_apply_curl_ca(curl);
}

/* 每会话语音设置 */
#define VOICE_MAX_SESSIONS 16
typedef struct {
    char chat_id[32];
    char voice[32];
    char format[8];
    time_t last_used;
} voice_session_t;

static voice_session_t s_sessions[VOICE_MAX_SESSIONS];

static const char *strip_data_url_prefix(const char *b64)
{
    if (!b64) return NULL;
    const char *p = strstr(b64, "base64,");
    return p ? (p + 7) : b64;
}

static voice_session_t *voice_session_get(const char *chat_id, bool create)
{
    if (!chat_id || !chat_id[0]) return NULL;
    int empty = -1;
    int lru = 0;
    time_t oldest = (time_t)LLONG_MAX;
    for (int i = 0; i < VOICE_MAX_SESSIONS; i++) {
        if (s_sessions[i].chat_id[0] == '\0') {
            if (empty < 0) empty = i;
            continue;
        }
        if (strcmp(s_sessions[i].chat_id, chat_id) == 0) {
            s_sessions[i].last_used = time(NULL);
            return &s_sessions[i];
        }
        if (s_sessions[i].last_used < oldest) {
            oldest = s_sessions[i].last_used;
            lru = i;
        }
    }
    if (!create) return NULL;
    int idx = (empty >= 0) ? empty : lru;
    memset(&s_sessions[idx], 0, sizeof(s_sessions[idx]));
    strncpy(s_sessions[idx].chat_id, chat_id, sizeof(s_sessions[idx].chat_id) - 1);
    strncpy(s_sessions[idx].voice, DEFAULT_VOICE, sizeof(s_sessions[idx].voice) - 1);
    strncpy(s_sessions[idx].format, DEFAULT_TTS_FORMAT, sizeof(s_sessions[idx].format) - 1);
    s_sessions[idx].last_used = time(NULL);
    return &s_sessions[idx];
}

/* 使用 multipart/form-data 发送音频到 ASR 接口（file 或 file_base64）。 */
static err_t voice_asr_mime(const unsigned char *audio_bytes,
                            size_t audio_len,
                            const char *audio_b64,
                            const char *model,
                            const char *prompt,
                            const char *hotwords_json,
                            const char *request_id,
                            const char *user_id,
                            char *out_text,
                            size_t out_size)
{
    if (!out_text || out_size == 0) return ERR_INVALID_ARG;
    if ((!audio_bytes || audio_len == 0) && (!audio_b64 || !audio_b64[0])) {
        return ERR_INVALID_ARG;
    }
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    const char *use_model = (model && model[0]) ? model : DEFAULT_ASR_MODEL;

    const char *b64 = audio_b64 ? strip_data_url_prefix(audio_b64) : NULL;

    pthread_once(&s_curl_once, curl_global_init_once);
    CURL *curl = curl_easy_init();
    if (!curl) return ERR_FAIL;

    buf_t resp = {0};
    struct curl_slist *headers = NULL;
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", s_bigmodel_key);
    headers = curl_slist_append(headers, auth);

    curl_easy_setopt(curl, CURLOPT_URL, BIGMODEL_ASR_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120 * 1000);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "agent-host/0.1");

    /* 组装 multipart */
    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = NULL;

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "model");
    curl_mime_data(part, use_model, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "stream");
    curl_mime_data(part, "false", CURL_ZERO_TERMINATED);

    /* 优先使用原始二进制 file 上传 */
    if (audio_bytes && audio_len > 0) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filename(part, "audio.wav");
        curl_mime_type(part, "drivers/audio/wav");
        curl_mime_data(part, (const char *)audio_bytes, (size_t)audio_len);
    } else if (b64 && b64[0]) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file_base64");
        curl_mime_data(part, b64, CURL_ZERO_TERMINATED);
    }

    if (prompt && prompt[0]) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "prompt");
        curl_mime_data(part, prompt, CURL_ZERO_TERMINATED);
    }
    if (hotwords_json && hotwords_json[0]) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "hotwords");
        curl_mime_data(part, hotwords_json, CURL_ZERO_TERMINATED);
    }
    if (request_id && request_id[0]) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "request_id");
        curl_mime_data(part, request_id, CURL_ZERO_TERMINATED);
    }
    if (user_id && user_id[0]) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "user_id");
        curl_mime_data(part, user_id, CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    apply_proxy(curl);
    apply_ca_cert(curl);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        pr_warn("ASR request failed: %s", curl_easy_strerror(res));
        kfree(resp.data);
        return ERR_FAIL;
    }

    if (status != 200 || !resp.data) {
        pr_warn("ASR failed: status=%ld body=%s", status, resp.data ? resp.data : "(null)");
        kfree(resp.data);
        return ERR_FAIL;
    }

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        kfree(resp.data);
        return ERR_FAIL;
    }

    const char *text = NULL;
    cJSON *text_item = cJSON_GetObjectItem(root, "text");
    if (text_item && cJSON_IsString(text_item)) {
        text = text_item->valuestring;
    }

    if (!text) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *t2 = cJSON_GetObjectItem(data, "text");
            if (t2 && cJSON_IsString(t2)) text = t2->valuestring;
        }
    }

    if (!text) {
        pr_warn("ASR response missing text: %s", resp.data);
        cJSON_Delete(root);
        kfree(resp.data);
        return ERR_FAIL;
    }

    strncpy(out_text, text, out_size - 1);
    out_text[out_size - 1] = '\0';
    pr_info("ASR text: %.256s", out_text);
    cJSON_Delete(root);
    kfree(resp.data);
    return 0;
}

static err_t voice_tts(const char *text,
                            const char *voice,
                            const char *format,
                            unsigned char **out_audio,
                            size_t *out_audio_len)
{
    if (!text || !text[0] || !out_audio) return ERR_INVALID_ARG;
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    const char *use_voice = (voice && voice[0]) ? voice : DEFAULT_VOICE;
    const char *use_format = (format && format[0]) ? format : DEFAULT_TTS_FORMAT;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", DEFAULT_TTS_MODEL);
    cJSON_AddStringToObject(body, "input", text);
    cJSON_AddStringToObject(body, "voice", use_voice);
    cJSON_AddStringToObject(body, "response_format", use_format);
    cJSON_AddNumberToObject(body, "sample_rate", 16000); /* robot hw supports 8k-16k */

    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) return ERR_NO_MEM;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", s_bigmodel_key);
    headers = curl_slist_append(headers, auth);

    host_http_response_t resp = {0};
    err_t err = host_http_request("POST", BIGMODEL_TTS_URL, headers, post_data, 30 * 1000, &resp);
    curl_slist_free_all(headers);
    kfree(post_data);

    if (err != 0) {
        host_http_response_free(&resp);
        return err;
    }

    if (resp.status != 200 || !resp.body || resp.body_len == 0) {
        pr_warn("TTS failed: status=%ld body_len=%zu", resp.status, resp.body_len);
        if (resp.body) {
            pr_warn("TTS error body: %.256s", resp.body);
        }
        host_http_response_free(&resp);
        return ERR_FAIL;
    }

    if (resp.headers && strstr(resp.headers, "Content-Type:") != NULL) {
        const char *ct = strstr(resp.headers, "Content-Type:");
        if (ct) {
            const char *end = strchr(ct, '\n');
            if (end && end > ct) {
                char tmp[128] = {0};
                size_t n = (size_t)(end - ct);
                if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
                memcpy(tmp, ct, n);
                pr_info("TTS response header: %s", tmp);
            }
        }
    }
    if (resp.body_len >= 4) {
        const unsigned char *b = (const unsigned char *)resp.body;
        pr_debug("TTS audio head: %02X %02X %02X %02X (%c%c%c%c) len=%zu", b[0], b[1], b[2], b[3], (b[0] >= 32 && b[0] <= 126) ? b[0] : '.', (b[1] >= 32 && b[1] <= 126) ? b[1] : '.', (b[2] >= 32 && b[2] <= 126) ? b[2] : '.', (b[3] >= 32 && b[3] <= 126) ? b[3] : '.', resp.body_len);
    }

    /* 直接返回 WAV 二进制 */
    size_t audio_len = resp.body_len;
    unsigned char *audio = kmalloc(audio_len, GFP_KERNEL);
    if (!audio) {
        host_http_response_free(&resp);
        return ERR_NO_MEM;
    }
    memcpy(audio, resp.body, audio_len);
    host_http_response_free(&resp);

    *out_audio = audio;
    if (out_audio_len) *out_audio_len = audio_len;
    return 0;
}

err_t voice_channel_init(void)
{
    const char *api_key = runtime_config_get_bigmodel_api_key();

    s_bigmodel_key[0] = '\0';
    if (api_key && api_key[0]) {
        strncpy(s_bigmodel_key, api_key, sizeof(s_bigmodel_key) - 1);
    }

    if (s_bigmodel_key[0]) {
        pr_info("Voice channel initialized (BigModel key loaded)");
    } else {
        pr_warn("Voice channel disabled: missing audio.bigmodel_api_key in config.json");
    }

    return 0;
}

err_t voice_channel_handle_audio_base64(const char *chat_id,
                                            const char *audio_base64,
                                            const char *asr_model,
                                            const char *prompt,
                                            const char *hotwords_json,
                                            const char *request_id,
                                            const char *user_id,
                                            const char *voice,
                                            const char *response_format)
{
    if (!chat_id || !audio_base64) return ERR_INVALID_ARG;
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    voice_session_t *sess = voice_session_get(chat_id, true);
    if (sess) {
        if (voice && voice[0]) {
            strncpy(sess->voice, voice, sizeof(sess->voice) - 1);
        }
        if (response_format && response_format[0]) {
            strncpy(sess->format, response_format, sizeof(sess->format) - 1);
        }
        sess->last_used = time(NULL);
    }

    char text[BUF_LARGE] = {0};
    err_t err = voice_asr_mime(NULL, 0, audio_base64, asr_model, prompt, hotwords_json,
                               request_id, user_id, text, sizeof(text));
    if (err != 0) {
        return err;
    }

    struct message msg = {0};
    strncpy(msg.channel, CHAN_VOICE, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    strncpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source) - 1);
    msg.content = strdup(text);
    if (!msg.content) return ERR_NO_MEM;

    return message_bus_push_inbound(&msg);
}

err_t voice_channel_send_reply(const char *chat_id, const char *text)
{
    if (!chat_id || !text) return ERR_INVALID_ARG;
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    voice_session_t *sess = voice_session_get(chat_id, false);
    const char *voice = sess ? sess->voice : DEFAULT_VOICE;
    const char *format = sess ? sess->format : DEFAULT_TTS_FORMAT;
    if (format && strcmp(format, "wav") != 0) {
        pr_warn("Audio output expects wav; overriding response_format=%s", format);
        format = DEFAULT_TTS_FORMAT;
    }

    unsigned char *audio = NULL;
    size_t audio_len = 0;
    err_t err = voice_tts(text, voice, format, &audio, &audio_len);
    if (err != 0) {
        return err;
    }

    pr_info("TTS audio bytes: %zu", audio_len);
    /* audio_output_play_wav 会解析 WAV 头，只播放 data 段 */
    err_t play_err = audio_output_play_wav(audio, audio_len);
    kfree(audio);
    if (play_err != 0) {
        pr_warn("Audio playback failed: %s", err_name(play_err));
    }
    return play_err;
}

err_t voice_channel_get_tts_pcm(const char *text,
                                     unsigned char **out_pcm,
                                     size_t *out_len,
                                     uint32_t *out_rate)
{
    if (!text || !text[0] || !out_pcm || !out_len) return ERR_INVALID_ARG;
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    /* Call BigModel TTS API → WAV bytes */
    unsigned char *wav = NULL;
    size_t wav_len = 0;
    err_t err = voice_tts(text, DEFAULT_VOICE, "wav", &wav, &wav_len);
    if (err != 0) return err;

    if (wav_len <= 44 || memcmp(wav, "RIFF", 4) != 0) {
        *out_pcm = wav;
        *out_len = wav_len;
        return 0;
    }

    /* Parse WAV: find "fmt " chunk for sample rate, "data" chunk for PCM */
    uint32_t sample_rate = 16000;
    unsigned char *pcm_data = NULL;
    size_t pcm_len = 0;
    size_t offset = 12; /* skip RIFF+WAVE */

    while (offset + 8 <= wav_len) {
        uint32_t chunk_id = *(uint32_t *)(wav + offset);
        uint32_t chunk_size = (uint32_t)wav[offset+4] | ((uint32_t)wav[offset+5] << 8) |
                             ((uint32_t)wav[offset+6] << 16) | ((uint32_t)wav[offset+7] << 24);

        if (chunk_id == 0x20746D66) { /* "fmt " */
            if (offset + 24 <= wav_len) {
                sample_rate = (uint32_t)wav[offset+12] | ((uint32_t)wav[offset+13] << 8) |
                             ((uint32_t)wav[offset+14] << 16) | ((uint32_t)wav[offset+15] << 24);
            }
        } else if (chunk_id == 0x61746164) { /* "data" */
            size_t start = offset + 8;
            pcm_len = start + chunk_size <= wav_len ? chunk_size : wav_len - start;
            pcm_data = wav + start;
            break; /* PCM found, stop parsing */
        }

        /* Advance: chunk header (8) + data (chunk_size), word-aligned */
        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++; /* pad byte */
    }

    if (!pcm_data || pcm_len == 0) {
        /* Fallback: skip 44-byte standard header */
        pcm_len = wav_len > 44 ? wav_len - 44 : 0;
        pcm_data = wav + 44;
    }

    /* Allocate and copy PCM, then free WAV */
    unsigned char *pcm = kmalloc(pcm_len, GFP_KERNEL);
    if (!pcm) { kfree(wav); return ERR_NO_MEM; }
    memcpy(pcm, pcm_data, pcm_len);

    /* Resample to 16000 Hz if needed (robot hardware max is 16025) */
    if (sample_rate > 16000) {
        size_t in_samples = pcm_len / 2;  /* 16-bit mono */
        size_t out_samples = (size_t)((uint64_t)in_samples * 16000 / sample_rate);
        unsigned char *resampled = kmalloc(out_samples * 2, GFP_KERNEL);
        if (resampled) {
            int16_t *in = (int16_t *)pcm;
            int16_t *out = (int16_t *)resampled;
            double ratio = (double)sample_rate / 16000.0;
            for (size_t i = 0; i < out_samples; i++) {
                double pos = (double)i * ratio;
                size_t idx = (size_t)pos;
                double frac = pos - (double)idx;
                if (idx + 1 < in_samples) {
                    double s = (double)in[idx]   * (1.0 - frac) +
                               (double)in[idx+1] * frac;
                    out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
                } else if (idx < in_samples) {
                    out[i] = in[idx];
                } else {
                    out[i] = 0;
                }
            }
            kfree(pcm);
            pcm = resampled;
            pcm_len = out_samples * 2;
            sample_rate = 16000;
        }
    }

    kfree(wav);
    *out_pcm = pcm;
    *out_len = pcm_len;
    if (out_rate) *out_rate = sample_rate;

    pr_info("TTS PCM: %zu bytes, sample_rate=%u Hz", pcm_len, sample_rate);
    return 0;
}

err_t voice_channel_handle_audio(const char *chat_id,
                                     const unsigned char *audio_bytes,
                                     size_t audio_len,
                                     const char *asr_model,
                                     const char *prompt,
                                     const char *hotwords_json,
                                     const char *request_id,
                                     const char *user_id,
                                     const char *voice,
                                     const char *response_format)
{
    if (!chat_id || !audio_bytes || audio_len == 0) return ERR_INVALID_ARG;
    if (!s_bigmodel_key[0]) return ERR_INVALID_STATE;

    voice_session_t *sess = voice_session_get(chat_id, true);
    if (sess) {
        if (voice && voice[0]) {
            strncpy(sess->voice, voice, sizeof(sess->voice) - 1);
        }
        if (response_format && response_format[0]) {
            strncpy(sess->format, response_format, sizeof(sess->format) - 1);
        }
        sess->last_used = time(NULL);
    }

    char text[BUF_LARGE] = {0};
    err_t err = voice_asr_mime(audio_bytes, audio_len, NULL,
                                    asr_model, prompt, hotwords_json,
                                    request_id, user_id, text, sizeof(text));
    if (err != 0) {
        return err;
    }

    /* Skip empty/noise ASR results to avoid LLM API errors */
    if (!text[0]) return 0;
    char *t = text;
    while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r') t++;
    if (!*t) return 0;
    if (strlen(t) <= 1 && (*t < 'A' || *t > 'z')) return 0;

    struct message msg = {0};
    strncpy(msg.channel, CHAN_VOICE, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    strncpy(msg.source, MSG_SOURCE_USER, sizeof(msg.source) - 1);
    msg.content = strdup(t);
    if (!msg.content) return ERR_NO_MEM;

    return message_bus_push_inbound(&msg);
}
