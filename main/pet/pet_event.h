#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Web 宠物通道的轻量协议常量。 */
#define PET_WS_TYPE_ACTION   "pet_action"
#define PET_WS_TYPE_RESPONSE "pet_response"

/* 前端当前会发送的互动动作；click 兼容旧别名。 */
#define PET_ACTION_TAP   "tap"
#define PET_ACTION_CLICK "click"
#define PET_ACTION_DRAG  "drag"
#define PET_ACTION_DROP  "drop"

/* 将宠物互动事件转换成给 LLM 的轻量 prompt。 */
char *pet_build_action_prompt(const char *action, const char *pet_id);

/* 根据主 Web chat_id 构造逻辑 pet chat_id，例如 pet_web_xxx。 */
bool pet_build_chat_id(const char *chat_id, char *out, size_t out_size);

/* 从逻辑 pet chat_id 还原真实 WebSocket chat_id。 */
bool pet_chat_id_to_ws_chat_id(const char *pet_chat_id, char *out, size_t out_size);

/* 为 pet 通道追加统一的通道提示词约束。 */
size_t pet_append_channel_policy_prompt(char *prompt, size_t size, size_t offset);
