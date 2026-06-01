/* 通道运行时分发：统一封装文本发送、工具活动和交互式提示。 */

#pragma once

#include <stdbool.h>

#include "bus/message_bus.h"
#include "daima_err.h"

typedef struct {
    const char *tool_name;
    const char *tool_input;
    const char *target;
    const char *detail;
    const char *default_text;
    bool ok;
    long elapsed_ms;
} daima_tool_activity_event_t;

daima_err_t channel_runtime_dispatch_outbound(const daima_msg_t *msg);
daima_err_t channel_runtime_send_tool_activity(const daima_msg_t *msg,
                                              const daima_tool_activity_event_t *event);
daima_err_t channel_runtime_request_sudo(const daima_msg_t *msg,
                                        const char *request_id,
                                        const char *prompt_text);
bool channel_runtime_wait_sudo_password(const daima_msg_t *msg,
                                        const char *request_id,
                                        char *password_out,
                                        size_t password_out_size);
