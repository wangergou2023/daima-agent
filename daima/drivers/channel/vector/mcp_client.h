/* 最小化 MCP (Model Context Protocol) JSON-RPC 2.0 客户端
 * 通过 popen 启动 robot-mcp 子进程，双向管道通信。
 *
 * 依赖: daima 已有的 cJSON
 */
#pragma once

#include "err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MCP 客户端上下文 */
typedef struct mcp_client mcp_client_t;

/* 音频通知回调类型
 * pcm: 16-bit signed little-endian PCM 数据
 * len: 字节数
 * timestamp: 序号
 */
typedef void (*mcp_audio_callback_t)(const uint8_t *pcm, size_t len, uint64_t timestamp, void *user_data);

/* 音频结束回调 — 机器人检测到说话结束，携带声源方向 + 传感器数据 */
typedef struct {
    uint16_t direction;
    uint16_t selectedDirection;
    int16_t  confidence;
    uint32_t prox_distance_mm;
    bool     prox_found_object;
    bool     prox_unobstructed;
    bool     cliff_detected;
    uint32_t robot_status;
    float    head_angle_deg;
} mcp_audio_direction_t;

typedef void (*mcp_audio_done_callback_t)(const mcp_audio_direction_t *dir, void *user_data);

/**
 * 启动 robot-mcp 子进程并完成 MCP 握手。
 * bin_path:   robot-mcp 可执行文件路径
 * robot_addr: gRPC 地址 (如 "localhost:443")
 * token_file: 令牌文件路径 (如 "/run/vic-cloud/perRuntimeToken")
 * 返回客户端指针，失败返回 NULL。
 */
mcp_client_t *mcp_client_launch(const char *bin_path, const char *robot_addr, const char *token_file);

/**
 * 关闭 MCP 客户端，终止子进程。
 */
void mcp_client_destroy(mcp_client_t *c);

/**
 * 调用 MCP 工具 (tools/call)。
 * tool_name: 工具名，如 "robot_drive_straight"
 * args_json: 参数 JSON 对象字符串，如 "{\"speed_mmps\":80,\"dist_mm\":100}"
 * response_out: 响应文本的输出缓冲区
 * response_size: 缓冲区大小
 */
daima_err_t mcp_client_call_tool(mcp_client_t *c, const char *tool_name,
                                const char *args_json,
                                char *response_out, size_t response_size);

/**
 * 查询工具列表 (tools/list)。
 * tools_json_out: 输出的工具列表 JSON 字符串
 * tools_size: 缓冲区大小
 */
daima_err_t mcp_client_list_tools(mcp_client_t *c, char *tools_json_out, size_t tools_size);

/**
 * 订阅音频流 (robot_subscribe_audio)。
 */
daima_err_t mcp_client_subscribe_audio(mcp_client_t *c);

/**
 * 取消音频订阅 (robot_unsubscribe_audio)。
 */
daima_err_t mcp_client_unsubscribe_audio(mcp_client_t *c);

/**
 * 注册音频通知回调。收到音频时调用 cb。
 */
void mcp_client_set_audio_callback(mcp_client_t *c, mcp_audio_callback_t cb, void *user_data);

/**
 * 注册音频结束回调 (机器人 VAD 检测到说话结束)。
 */
void mcp_client_set_audio_done_callback(mcp_client_t *c, mcp_audio_done_callback_t cb, void *user_data);

/**
 * 轮询 MCP 子进程，处理待处理的响应和通知（非阻塞）。
 * 应在主循环中定期调用。
 * 返回处理的消息数量。
 */
int mcp_client_poll(mcp_client_t *c);

/**
 * 关闭子进程的 stdin（用于测试）。
 */
void mcp_client_close_stdin(mcp_client_t *c);

#ifdef __cplusplus
}
#endif
