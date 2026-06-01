/* 设备音频输入/输出接口（占位）。 */

#pragma once

#include "daima_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sample_rate;      /* 例如 16000 */
    int channels;         /* 1=mono, 2=stereo */
    int bits_per_sample;  /* 例如 16 */
} audio_stream_cfg_t;

daima_err_t audio_input_start(const audio_stream_cfg_t *cfg);
daima_err_t audio_input_read(uint8_t *buf, size_t buf_size, size_t *out_size);
void audio_input_stop(void);

daima_err_t audio_output_start(const audio_stream_cfg_t *cfg);
daima_err_t audio_output_write(const uint8_t *buf, size_t buf_size);
void audio_output_stop(void);

/* 便捷接口：播放 WAV 音频（默认实现为占位）。 */
daima_err_t audio_output_play_wav(const uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
