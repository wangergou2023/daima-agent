/* 飞书事件解析与消息入站处理。 */

#pragma once

#include <stddef.h>

void feishu_event_handler_process_ws_event_json(const char *app_id,
                                                const char *app_secret,
                                                const char *json,
                                                size_t len);
