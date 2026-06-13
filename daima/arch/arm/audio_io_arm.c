/* 设备音频输入/输出接口（Host 占位实现）。 */

#include "drivers/audio/audio_io.h"
#include "linux/printk.h"
daima_err_t audio_input_start(const audio_stream_cfg_t *cfg)
{
    (void)cfg;
    pr_warn("audio_input_start not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

daima_err_t audio_input_read(uint8_t *buf, size_t buf_size, size_t *out_size)
{
    (void)buf;
    (void)buf_size;
    if (out_size) *out_size = 0;
    pr_warn("audio_input_read not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

void audio_input_stop(void)
{
    pr_warn("audio_input_stop not implemented");
}

daima_err_t audio_output_start(const audio_stream_cfg_t *cfg)
{
    (void)cfg;
    pr_warn("audio_output_start not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

daima_err_t audio_output_write(const uint8_t *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    pr_warn("audio_output_write not implemented");
    return DAIMA_ERR_INVALID_STATE;
}

void audio_output_stop(void)
{
    pr_warn("audio_output_stop not implemented");
}

daima_err_t audio_output_play_wav(const uint8_t *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    pr_warn("audio_output_play_wav not implemented");
    return DAIMA_ERR_INVALID_STATE;
}
