/* 设备音频输入/输出接口（MIPS/IMP 实现）。 */

#include "drivers/audio/audio_io.h"
#include "runtime.h"
#include "autoconf.h"
#include "linux/printk.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <imp/imp_audio.h>

static const char *TAG = "audio_io";

#ifndef BUILD_FOR_MIPS
#error "audio_io_mips.c must be built only when BUILD_FOR_MIPS is enabled"
#endif

static bool s_ai_started = false;
static bool s_ao_started = false;
static audio_stream_cfg_t s_ai_cfg = {0};
static audio_stream_cfg_t s_ao_cfg = {0};
static size_t s_ao_frame_bytes = 0;

static int imp_ai_dev(void) { return DAIMA_AUDIO_AI_DEV_ID; }
static int imp_ai_chn(void) { return DAIMA_AUDIO_AI_CHN_ID; }
static int imp_ao_dev(void) { return DAIMA_AUDIO_AO_DEV_ID; }
static int imp_ao_chn(void) { return DAIMA_AUDIO_AO_CHN_ID; }

static int bytes_per_frame(const audio_stream_cfg_t *cfg, int frame_ms)
{
    if (!cfg || cfg->sample_rate <= 0 || cfg->channels <= 0 || cfg->bits_per_sample <= 0) return 0;
    int samples = (cfg->sample_rate * frame_ms) / 1000;
    int bytes_per_sample = cfg->bits_per_sample / 8;
    return samples * cfg->channels * bytes_per_sample;
}

daima_err_t audio_input_start(const audio_stream_cfg_t *cfg)
{
    if (!cfg) return DAIMA_ERR_INVALID_ARG;
    if (s_ai_started &&
        memcmp(&s_ai_cfg, cfg, sizeof(*cfg)) == 0) {
        return DAIMA_OK;
    }

    int devID = imp_ai_dev();
    int chnID = imp_ai_chn();
    int ret;

    IMPAudioIOAttr attr;
    memset(&attr, 0, sizeof(attr));
    attr.samplerate = (IMPAudioSampleRate)cfg->sample_rate;
    attr.bitwidth = (IMPAudioBitWidth)cfg->bits_per_sample;
    attr.soundmode = (cfg->channels == 1) ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
    attr.frmNum = DAIMA_AUDIO_AI_FRM_NUM;
    attr.numPerFrm = (cfg->sample_rate * DAIMA_AUDIO_FRAME_MS) / 1000;
    attr.chnCnt = 1;

    ret = IMP_AI_SetPubAttr(devID, &attr);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AI_SetPubAttr failed: %d", ret);
        return DAIMA_FAIL;
    }

    ret = IMP_AI_Enable(devID);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AI_Enable failed: %d", ret);
        return DAIMA_FAIL;
    }

    IMPAudioIChnParam chnParam;
    memset(&chnParam, 0, sizeof(chnParam));
    chnParam.usrFrmDepth = DAIMA_AUDIO_AI_FRM_NUM;
    chnParam.aecChn = 0;
    ret = IMP_AI_SetChnParam(devID, chnID, &chnParam);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AI_SetChnParam failed: %d", ret);
        return DAIMA_FAIL;
    }

    ret = IMP_AI_EnableChn(devID, chnID);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AI_EnableChn failed: %d", ret);
        return DAIMA_FAIL;
    }

    int ai_vol = runtime_config_get_audio_ai_vol();
    int ai_gain = runtime_config_get_audio_ai_gain();
    IMP_AI_SetVol(devID, chnID, ai_vol);
    IMP_AI_SetGain(devID, chnID, ai_gain);
    IMP_AI_EnableAlgo(devID, chnID);

    s_ai_started = true;
    s_ai_cfg = *cfg;
    DAIMA_LOGI(TAG, "Audio input started: %d Hz, %d ch, %d bit",
             cfg->sample_rate, cfg->channels, cfg->bits_per_sample);
    return DAIMA_OK;
}

daima_err_t audio_input_read(uint8_t *buf, size_t buf_size, size_t *out_size)
{
    if (!buf || buf_size == 0) return DAIMA_ERR_INVALID_ARG;
    if (!s_ai_started) return DAIMA_ERR_INVALID_STATE;
    if (out_size) *out_size = 0;

    int devID = imp_ai_dev();
    int chnID = imp_ai_chn();

    int ret = IMP_AI_PollingFrame(devID, chnID, 1000);
    if (ret != 0) {
        return DAIMA_ERR_TIMEOUT;
    }

    IMPAudioFrame frm;
    ret = IMP_AI_GetFrame(devID, chnID, &frm, BLOCK);
    if (ret != 0) {
        return DAIMA_FAIL;
    }

    size_t copy = frm.len < (int)buf_size ? (size_t)frm.len : buf_size;
    memcpy(buf, frm.virAddr, copy);

    ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
    if (ret != 0) {
        return DAIMA_FAIL;
    }

    if (out_size) *out_size = copy;
    return DAIMA_OK;
}

void audio_input_stop(void)
{
    if (!s_ai_started) return;
    int devID = imp_ai_dev();
    int chnID = imp_ai_chn();
    IMP_AI_DisableAlgo(devID, chnID);
    IMP_AI_DisableChn(devID, chnID);
    IMP_AI_Disable(devID);
    s_ai_started = false;
    memset(&s_ai_cfg, 0, sizeof(s_ai_cfg));
}

daima_err_t audio_output_start(const audio_stream_cfg_t *cfg)
{
    if (!cfg) return DAIMA_ERR_INVALID_ARG;
    if (s_ao_started &&
        memcmp(&s_ao_cfg, cfg, sizeof(*cfg)) == 0) {
        return DAIMA_OK;
    }

    int devID = imp_ao_dev();
    int chnID = imp_ao_chn();
    int ret;

    IMPAudioIOAttr attr;
    memset(&attr, 0, sizeof(attr));
    attr.samplerate = (IMPAudioSampleRate)cfg->sample_rate;
    attr.bitwidth = (IMPAudioBitWidth)cfg->bits_per_sample;
    attr.soundmode = (cfg->channels == 1) ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
    attr.frmNum = DAIMA_AUDIO_AO_FRM_NUM;
    attr.numPerFrm = (cfg->sample_rate * DAIMA_AUDIO_FRAME_MS) / 1000;
    attr.chnCnt = 1;

    ret = IMP_AO_SetPubAttr(devID, &attr);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AO_SetPubAttr failed: %d", ret);
        return DAIMA_FAIL;
    }

    ret = IMP_AO_Enable(devID);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AO_Enable failed: %d", ret);
        return DAIMA_FAIL;
    }

    ret = IMP_AO_EnableChn(devID, chnID);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AO_EnableChn failed: %d", ret);
        return DAIMA_FAIL;
    }

    int ao_vol = runtime_config_get_audio_ao_vol();
    int ao_gain = runtime_config_get_audio_ao_gain();
    IMP_AO_SetVol(devID, chnID, ao_vol);
    IMP_AO_SetGain(devID, chnID, ao_gain);
    IMP_AO_EnableAlgo(devID, chnID);

    s_ao_started = true;
    s_ao_cfg = *cfg;
    s_ao_frame_bytes = (size_t)bytes_per_frame(cfg, DAIMA_AUDIO_FRAME_MS);
    DAIMA_LOGI(TAG, "Audio output started: %d Hz, %d ch, %d bit",
             cfg->sample_rate, cfg->channels, cfg->bits_per_sample);
    return DAIMA_OK;
}

daima_err_t audio_output_write(const uint8_t *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return DAIMA_ERR_INVALID_ARG;
    if (!s_ao_started) return DAIMA_ERR_INVALID_STATE;

    int devID = imp_ao_dev();
    int chnID = imp_ao_chn();

    IMPAudioFrame frm;
    frm.virAddr = (uint32_t *)buf;
    frm.len = (int)buf_size;
    int ret = IMP_AO_SendFrame(devID, chnID, &frm, BLOCK);
    if (ret != 0) {
        DAIMA_LOGE(TAG, "IMP_AO_SendFrame failed: %d (len=%zu)", ret, buf_size);
        return DAIMA_FAIL;
    }
    return DAIMA_OK;
}

void audio_output_stop(void)
{
    if (!s_ao_started) return;
    int devID = imp_ao_dev();
    int chnID = imp_ao_chn();
    IMP_AO_FlushChnBuf(devID, chnID);
    IMP_AO_DisableAlgo(devID, chnID);
    IMP_AO_DisableChn(devID, chnID);
    IMP_AO_Disable(devID);
    s_ao_started = false;
    s_ao_frame_bytes = 0;
    memset(&s_ao_cfg, 0, sizeof(s_ao_cfg));
}

static bool wav_parse(const uint8_t *buf, size_t len,
                      audio_stream_cfg_t *cfg,
                      const uint8_t **data,
                      size_t *data_len)
{
    if (!buf || len < 12) return false;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;

    size_t off = 12;
    bool got_fmt = false;
    bool got_data = false;
    audio_stream_cfg_t tmp = {0};
    const uint8_t *data_ptr = NULL;
    size_t data_size = 0;

    while (off + 8 <= len) {
        const uint8_t *chunk = buf + off;
        uint32_t chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
        const uint8_t *payload = chunk + 8;
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16 && off + 8 + chunk_size <= len) {
            uint16_t audio_format = payload[0] | (payload[1] << 8);
            uint16_t channels = payload[2] | (payload[3] << 8);
            uint32_t sample_rate = payload[4] | (payload[5] << 8) | (payload[6] << 16) | (payload[7] << 24);
            uint16_t bits = payload[14] | (payload[15] << 8);
            if (audio_format != 1) return false;
            tmp.sample_rate = (int)sample_rate;
            tmp.channels = (int)channels;
            tmp.bits_per_sample = (int)bits;
            got_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0 && off + 8 + chunk_size <= len) {
            data_ptr = payload;
            data_size = chunk_size;
            got_data = true;
        }

        size_t step = 8 + chunk_size;
        if (step & 1) step++;
        off += step;
    }

    if (!got_fmt || !got_data || !data_ptr) return false;
    if (cfg) *cfg = tmp;
    if (data) *data = data_ptr;
    if (data_len) *data_len = data_size;
    return true;
}

daima_err_t audio_output_play_wav(const uint8_t *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return DAIMA_ERR_INVALID_ARG;

    audio_stream_cfg_t cfg = {0};
    const uint8_t *data = NULL;
    size_t data_len = 0;
    if (!wav_parse(buf, buf_size, &cfg, &data, &data_len)) {
        DAIMA_LOGE(TAG, "Invalid WAV data");
        return DAIMA_ERR_INVALID_ARG;
    }

    if (cfg.bits_per_sample != 16) {
        DAIMA_LOGE(TAG, "Unsupported WAV bits_per_sample=%d", cfg.bits_per_sample);
        return DAIMA_ERR_INVALID_ARG;
    }

    DAIMA_LOGI(TAG, "WAV cfg: %d Hz, %d ch, %d bit, data_len=%zu",
             cfg.sample_rate, cfg.channels, cfg.bits_per_sample, data_len);

    daima_err_t err = audio_output_start(&cfg);
    if (err != DAIMA_OK) return err;

    size_t offset = 0;
    size_t chunk = s_ao_frame_bytes ? s_ao_frame_bytes : data_len;
    while (offset < data_len) {
        size_t n = (data_len - offset) < chunk ? (data_len - offset) : chunk;
        err = audio_output_write(data + offset, n);
        if (err != DAIMA_OK) break;
        offset += n;
    }

    return err;
}
