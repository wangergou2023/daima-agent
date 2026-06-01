/* 语音通道（语音转文本 / 文本转语音）。 */

#pragma once

#include "daima_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化语音通道（加载 API Key）。
 */
daima_err_t voice_channel_init(void);

/**
 * 处理语音请求（base64 音频 -> ASR -> 推入消息总线）。
 * audio_base64 可包含 data:...;base64, 前缀。
 */
daima_err_t voice_channel_handle_audio_base64(const char *chat_id,
                                            const char *audio_base64,
                                            const char *asr_model,
                                            const char *prompt,
                                            const char *hotwords_json,
                                            const char *request_id,
                                            const char *user_id,
                                            const char *voice,
                                            const char *response_format);

/**
 * 处理语音请求（原始音频字节 -> base64 -> ASR -> 推入消息总线）。
 * audio_bytes 应为完整音频文件（如 WAV/MP3）的字节内容。
 */
daima_err_t voice_channel_handle_audio(const char *chat_id,
                                     const unsigned char *audio_bytes,
                                     size_t audio_len,
                                     const char *asr_model,
                                     const char *prompt,
                                     const char *hotwords_json,
                                     const char *request_id,
                                     const char *user_id,
                                     const char *voice,
                                     const char *response_format);

/**
 * 将文本回复转为语音并交给设备播放。
 */
daima_err_t voice_channel_send_reply(const char *chat_id, const char *text);

/**
 * 将文本转为 PCM 音频（通过 BigModel TTS API）。
 * 返回 16-bit signed LE PCM 数据，调用方需 free(*out_pcm)。
 *
 * @param text       要转换的文本
 * @param out_pcm    输出: PCM 数据 (16-bit signed LE)
 * @param out_len    输出: PCM 字节数
 * @param out_rate   输出: 采样率 (Hz)，如 16000 或 24000
 * @return DAIMA_OK 成功，其他为失败
 */
daima_err_t voice_channel_get_tts_pcm(const char *text,
                                     unsigned char **out_pcm,
                                     size_t *out_len,
                                     uint32_t *out_rate);

#ifdef __cplusplus
}
#endif
