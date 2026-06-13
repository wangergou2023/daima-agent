/* 会话搜索工具。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/*
 * 搜索历史会话消息与事实卡片，或列出已有会话。
 * 输入 JSON：
 * - {"query":"sudo","target":"both","limit":8}
 * - {"chat_id":"web_1dfazy","target":"facts"}
 * - {"output_mode":"sessions"}  // 不传 query 时列出会话概览
 */
daima_err_t tool_session_search_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_session_search_definition(void);
