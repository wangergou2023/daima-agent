/* Vector 机器人通道 — 通过 MCP 与 robot-mcp 通信
 *
 * 功能:
 *   - 启动 robot-mcp 子进程
 *   - 订阅音频流，将 PCM 音频送入现有 ASR 管道 (voice_channel)
 *   - 将 LLM 回复通过 TTS + Unix socket 播放到机器人扬声器
 */
#pragma once

#include "daima_err.h"
#include "channels/vector/mcp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

daima_err_t vector_channel_init(void);
daima_err_t vector_channel_start(void);

/**
 * 将文本回复发送到 Vector 扬声器。
 * 实际音频播放由 TTS 管线生成 PCM 后经 /tmp/daima_spk.sock 发送给 robot-mcp。
 */
daima_err_t vector_channel_send_reply(const char *chat_id, const char *text);

/**
 * 将 PCM 音频通过 robot-mcp 播放到 Vector 扬声器。
 * pcm: 16-bit signed LE 原始 PCM 数据
 * pcm_len: 字节数
 * sample_rate: 采样率 (Hz)，如 16000 或 24000
 */
daima_err_t vector_channel_play_pcm(const unsigned char *pcm, size_t pcm_len, uint32_t sample_rate, uint32_t seq, const char *label);

mcp_client_t *vector_channel_get_mcp(void);

void vector_channel_mute_mic(bool mute);

/**
 * 获取最近一次声源方向。
 * 返回方向索引 (0-11 = 水平方向, 12 = 头顶, 0xFF = 无数据)。
 */
uint16_t vector_channel_get_mic_direction(void);

/**
 * 传感器数据快照。
 */
typedef struct {
    uint32_t prox_distance_mm;
    bool     prox_found_object;
    bool     prox_unobstructed;
    bool     cliff_detected;
    uint32_t robot_status;
    float    head_angle_deg;
} vector_sensor_snapshot_t;

void vector_channel_get_sensor_snapshot(vector_sensor_snapshot_t *out);

#ifdef __cplusplus
}
#endif
