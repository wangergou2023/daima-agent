/* 语音唤醒按键与录音处理（MIPS/sysfs）。 */

#include "drivers/voice/voice_wake.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/audio/audio_io.h"
#include "runtime.h"
#include "autoconf.h"
#include "os.h"
#include "linux/printk.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "linux/slab.h"

static const char *TAG = "voice_wake";

#ifndef BUILD_FOR_MIPS
#error "voice_wake_mips.c must be built only when BUILD_FOR_MIPS is enabled"
#endif

typedef struct {
    int gpio;
    int fd;
    int pressed_value;
    int last_value;
    int active_low;
    uint64_t last_press_ms;
} wake_gpio_t;

static bool s_running = false;
static bool s_recording = false;
static daima_task_t *s_task = NULL;
static int s_poll_ms = DEFAULT_WAKE_GPIO_POLL_MS;
static int s_debounce_ms = DEFAULT_WAKE_GPIO_DEBOUNCE_MS;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static int write_str(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return (n == (ssize_t)strlen(value)) ? 0 : -1;
}

static bool path_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static int gpio_export(int gpio)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (path_exists(path)) {
        DAIMA_LOGI(TAG, "GPIO %d already exported", gpio);
        return 0;
    }

    char num[16];
    snprintf(num, sizeof(num), "%d", gpio);
    DAIMA_LOGI(TAG, "Exporting GPIO %d", gpio);
    if (write_str("/sys/class/gpio/export", num) != 0) {
        if (errno != EBUSY) {
            DAIMA_LOGE(TAG, "GPIO %d export failed: %s", gpio, strerror(errno));
            return -1;
        }
    }

    for (int i = 0; i < 50; i++) {
        if (path_exists(path)) return 0;
        daima_task_delay(10);
    }
    DAIMA_LOGE(TAG, "GPIO %d export timeout", gpio);
    return -1;
}

static int gpio_set_direction_in(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    if (write_str(path, "in") != 0) {
        DAIMA_LOGE(TAG, "GPIO %d set direction failed: %s", gpio, strerror(errno));
        return -1;
    }
    return 0;
}

static int gpio_set_active_low(int gpio, int active_low)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/active_low", gpio);
    char val[4];
    snprintf(val, sizeof(val), "%d", active_low ? 1 : 0);
    if (write_str(path, val) != 0) {
        DAIMA_LOGW(TAG, "GPIO %d active_low not supported", gpio);
        return -1;
    }
    return 0;
}

static int read_file_int(const char *path, int *out)
{
    if (!out) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[8] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    *out = atoi(buf);
    return 0;
}

static int gpio_read_active_low(int gpio, int *out_active_low)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/active_low", gpio);
    return read_file_int(path, out_active_low);
}

static int gpio_open_value(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        DAIMA_LOGE(TAG, "GPIO %d open value failed: %s", gpio, strerror(errno));
    }
    return fd;
}

static int gpio_read_value(int fd)
{
    if (fd < 0) return -1;
    char ch = '0';
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, &ch, 1);
    if (n <= 0) return -1;
    return (ch == '0') ? 0 : 1;
}

static daima_err_t wake_gpio_init(wake_gpio_t *wg)
{
    if (!wg) return DAIMA_ERR_INVALID_ARG;
    memset(wg, 0, sizeof(*wg));

    wg->gpio = runtime_config_get_wake_gpio_num();
    if (gpio_export(wg->gpio) != 0) return DAIMA_FAIL;
    if (gpio_set_direction_in(wg->gpio) != 0) return DAIMA_FAIL;
    int active_low_cfg = runtime_config_get_wake_gpio_active_low();
    gpio_set_active_low(wg->gpio, active_low_cfg);

    wg->fd = gpio_open_value(wg->gpio);
    if (wg->fd < 0) return DAIMA_FAIL;

    wg->active_low = -1;
    if (gpio_read_active_low(wg->gpio, &wg->active_low) != 0) {
        wg->active_low = active_low_cfg ? 1 : 0;
    }
    wg->pressed_value = (wg->active_low != 0) ? 1 : 0;
    wg->last_value = gpio_read_value(wg->fd);
    wg->last_press_ms = 0;

    s_poll_ms = runtime_config_get_wake_gpio_poll_ms();
    s_debounce_ms = runtime_config_get_wake_gpio_debounce_ms();

    DAIMA_LOGI(TAG, "Wake GPIO listener started (gpio=%d, active_low=%d, pressed_value=%d, poll=%dms, debounce=%dms)",
              wg->gpio, wg->active_low, wg->pressed_value,
              s_poll_ms, s_debounce_ms);
    DAIMA_LOGI(TAG, "Wake GPIO initial value=%d", wg->last_value);
    return DAIMA_OK;
}

static void wake_gpio_close(wake_gpio_t *wg)
{
    if (!wg) return;
    if (wg->fd >= 0) {
        close(wg->fd);
        wg->fd = -1;
    }
}

static bool wake_gpio_poll_pressed(wake_gpio_t *wg)
{
    if (!wg || wg->fd < 0) return false;
    int value = gpio_read_value(wg->fd);
    if (value < 0) return false;

    bool edge = (value == wg->pressed_value && wg->last_value != wg->pressed_value);
    wg->last_value = value;
    if (!edge) return false;

    uint64_t now = now_ms();
    if (now - wg->last_press_ms < (uint64_t)s_debounce_ms) return false;
    wg->last_press_ms = now;
    return true;
}

static size_t calc_frame_bytes(const audio_stream_cfg_t *cfg, int frame_ms)
{
    if (!cfg || frame_ms <= 0) return 0;
    if (cfg->sample_rate <= 0 || cfg->channels <= 0 || cfg->bits_per_sample <= 0) return 0;
    size_t samples = (size_t)cfg->sample_rate * (size_t)frame_ms / 1000U;
    size_t bytes_per_sample = (size_t)cfg->bits_per_sample / 8U;
    return samples * (size_t)cfg->channels * bytes_per_sample;
}

static bool write_le16(uint8_t *buf, size_t off, uint16_t v, size_t cap)
{
    if (off + 2 > cap) return false;
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    return true;
}

static bool write_le32(uint8_t *buf, size_t off, uint32_t v, size_t cap)
{
    if (off + 4 > cap) return false;
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    return true;
}

static unsigned char *build_wav(const uint8_t *pcm,
                               size_t pcm_len,
                               const audio_stream_cfg_t *cfg,
                               size_t *out_len)
{
    if (!pcm || !cfg || pcm_len == 0) return NULL;
    const size_t header = 44;
    size_t total = header + pcm_len;
    unsigned char *buf = kmalloc(total, GFP_KERNEL);
    if (!buf) return NULL;

    memset(buf, 0, header);
    memcpy(buf, "RIFF", 4);
    write_le32(buf, 4, (uint32_t)(36 + pcm_len), header);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    write_le32(buf, 16, 16, header);
    write_le16(buf, 20, 1, header);
    write_le16(buf, 22, (uint16_t)cfg->channels, header);
    write_le32(buf, 24, (uint32_t)cfg->sample_rate, header);
    uint32_t byte_rate = (uint32_t)(cfg->sample_rate * cfg->channels * cfg->bits_per_sample / 8);
    write_le32(buf, 28, byte_rate, header);
    uint16_t block_align = (uint16_t)(cfg->channels * cfg->bits_per_sample / 8);
    write_le16(buf, 32, block_align, header);
    write_le16(buf, 34, (uint16_t)cfg->bits_per_sample, header);
    memcpy(buf + 36, "data", 4);
    write_le32(buf, 40, (uint32_t)pcm_len, header);

    memcpy(buf + header, pcm, pcm_len);
    if (out_len) *out_len = total;
    return buf;
}

static void fill_default_cfg(audio_stream_cfg_t *cfg)
{
    if (!cfg) return;
    cfg->sample_rate = DAIMA_AUDIO_SAMPLE_RATE;
    cfg->channels = DAIMA_AUDIO_CHANNELS;
    cfg->bits_per_sample = DAIMA_AUDIO_BITS_PER_SAMPLE;
}

static daima_err_t capture_wav(unsigned char **out_wav, size_t *out_len)
{
    if (!out_wav || !out_len) return DAIMA_ERR_INVALID_ARG;
    *out_wav = NULL;
    *out_len = 0;

    audio_stream_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    fill_default_cfg(&cfg);

    size_t bytes_per_ms = (size_t)cfg.sample_rate * (size_t)cfg.channels * (size_t)cfg.bits_per_sample / 8U / 1000U;
    int record_ms = runtime_config_get_voice_record_ms();
    size_t expected = bytes_per_ms * (size_t)record_ms;
    if (bytes_per_ms == 0 || expected == 0) return DAIMA_ERR_INVALID_STATE;

    DAIMA_LOGI(TAG, "Record cfg: %d Hz, %d ch, %d bit, %d ms (~%zu bytes)",
              cfg.sample_rate, cfg.channels, cfg.bits_per_sample,
              record_ms, expected);

    uint8_t *pcm = kmalloc(expected, GFP_KERNEL);
    if (!pcm) return DAIMA_ERR_NO_MEM;
    size_t offset = 0;

    daima_err_t err = audio_input_start(&cfg);
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "audio_input_start failed: %s", daima_err_to_name(err));
        kfree(pcm);
        return err;
    }

    size_t frame_bytes = calc_frame_bytes(&cfg, DAIMA_AUDIO_FRAME_MS);
    if (frame_bytes == 0) frame_bytes = 2048;
    DAIMA_LOGI(TAG, "Audio frame bytes: %zu", frame_bytes);

    uint8_t *frame = kmalloc(frame_bytes, GFP_KERNEL);
    if (!frame) {
        audio_input_stop();
        kfree(pcm);
        return DAIMA_ERR_NO_MEM;
    }

    int timeout_count = 0;
    while (offset < expected) {
        size_t got = 0;
        err = audio_input_read(frame, frame_bytes, &got);
        if (err == DAIMA_ERR_TIMEOUT) {
            if (++timeout_count > 10) break;
            continue;
        }
        if (err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "audio_input_read failed: %s", daima_err_to_name(err));
            break;
        }
        timeout_count = 0;
        if (got == 0) continue;
        size_t remain = expected - offset;
        size_t take = got > remain ? remain : got;
        memcpy(pcm + offset, frame, take);
        offset += take;
    }

    kfree(frame);
    audio_input_stop();

    if (offset == 0) {
        DAIMA_LOGW(TAG, "No audio captured");
        kfree(pcm);
        return DAIMA_FAIL;
    }

    unsigned char *wav = build_wav(pcm, offset, &cfg, out_len);
    kfree(pcm);
    if (!wav) return DAIMA_ERR_NO_MEM;

    DAIMA_LOGI(TAG, "Captured %zu bytes PCM, WAV size %zu", offset, *out_len);
    *out_wav = wav;
    return DAIMA_OK;
}

static daima_err_t capture_and_send(void)
{
    unsigned char *wav = NULL;
    size_t wav_len = 0;

    daima_err_t err = capture_wav(&wav, &wav_len);
    if (err != DAIMA_OK) return err;

    err = voice_channel_handle_audio(
        DAIMA_VOICE_CHAT_ID, wav, wav_len, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    kfree(wav);
    return err;
}

static void voice_wake_task(void *arg)
{
    (void)arg;
    wake_gpio_t gpio;
    if (wake_gpio_init(&gpio) != DAIMA_OK) {
        s_running = false;
        return;
    }

    while (s_running) {
        if (!s_recording && wake_gpio_poll_pressed(&gpio)) {
            s_recording = true;
            int record_ms = runtime_config_get_voice_record_ms();
            DAIMA_LOGI(TAG, "Wake GPIO pressed, recording %d ms", record_ms);
            daima_err_t err = capture_and_send();
            if (err != DAIMA_OK) {
                DAIMA_LOGW(TAG, "Voice capture failed: %s", daima_err_to_name(err));
            }
            s_recording = false;
        }
        daima_task_delay((uint32_t)s_poll_ms);
    }

    wake_gpio_close(&gpio);
    DAIMA_LOGI(TAG, "Wake GPIO listener stopped");
}

daima_err_t voice_wake_start(void)
{
    if (s_running) return DAIMA_OK;
    s_running = true;
    s_recording = false;
    DAIMA_LOGI(TAG, "Starting wake GPIO thread");
    if (!daima_task_create(voice_wake_task, "voice_wake",
                          4096, NULL, 4, &s_task)) {
        s_running = false;
        return DAIMA_FAIL;
    }
    return DAIMA_OK;
}

void voice_wake_stop(void)
{
    if (!s_running) return;
    DAIMA_LOGI(TAG, "Stopping wake GPIO thread");
    s_running = false;
    if (s_task) {
        daima_task_delete(s_task);
        s_task = NULL;
    }
}
