/* 交互模式接口。
 * 支持需要用户交互确认的操作（如 sudo 密码输入）。
 * 通过通道向用户发送交互请求并等待响应。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"
#include "err.h"

/* 向用户发送 sudo 密码请求（交互式提权） */
err_t channel_runtime_request_sudo(const struct message *msg,
					const char *request_id,
					const char *prompt_text);

/* 等待用户输入 sudo 密码（阻塞等待，直到超时或收到回复） */
bool channel_runtime_wait_sudo_password(const struct message *msg,
					const char *request_id,
					char *password_out,
					size_t password_out_size);
