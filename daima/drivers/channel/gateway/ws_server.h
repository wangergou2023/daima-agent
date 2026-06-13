/* WebSocket 服务接口定义。 */

#pragma once

#include "err.h"

/**
 * 在 runtime config 指定端口上初始化并启动 WebSocket 服务器。
 * 允许外部客户端通过 JSON 消息与智能体交互。
 *
 * 协议（Web UI 与内置 WebSocket 服务复用同一端口）：
 *   入站：  {"type":"message","content":"hello","chat_id":"ws_client1"}
 *   出站：  {"type":"response","content":"Hi!","chat_id":"ws_client1"}
 *   宠物入站：{"type":"pet_action","action":"tap","chat_id":"web_xxx","pet_chat_id":"pet_web_xxx","pet_id":"guga"}
 *   宠物出站：{"type":"pet_response","content":"咕嘎~","chat_id":"pet_web_xxx"}
 */
daima_err_t ws_server_start(void);

/**
 * 通过 chat_id 向指定 WebSocket 客户端发送文本消息。
 * @param chat_id  客户端标识（连接时分配）
 * @param text     消息文本
 */
daima_err_t ws_server_send(const char *chat_id, const char *text);
daima_err_t ws_server_send_with_reasoning(const char *chat_id, const char *text, const char *reasoning);

/**
 * 发送轻量工具活动消息，用于 Web 对话流中的过程提示。
 */
daima_err_t ws_server_send_tool_event(const char *chat_id, const char *text);

/**
 * 发送宠物通道回复；逻辑 chat_id 为 pet_chat_id，底层仍复用对应 WebSocket 连接。
 * chat_id 映射与 prompt 规则统一收敛在 main/pet/ 下。
 */
daima_err_t ws_server_send_pet_response(const char *pet_chat_id, const char *text);

/**
 * 发送 sudo 密码请求，让 Web UI 展示密码输入框。
 */
daima_err_t ws_server_send_sudo_request(const char *chat_id, const char *request_id, const char *prompt_text);

/**
 * 停止 WebSocket 服务器。
 */
daima_err_t ws_server_stop(void);
