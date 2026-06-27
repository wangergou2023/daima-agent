/* 交互模式接口。
 * 支持需要用户交互确认的操作（如 sudo 密码输入）。
 * 通过通道向用户发送交互请求并等待响应。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"
#include "err.h"

typedef struct interactive_request_meta {
	const char *request_type;
	const char *request_id;
	const char *prompt_text;
	const char *task_id;
	const char *session_id;
	const char *coordinator_id;
} interactive_request_meta_t;

typedef struct interactive_reply {
	char value[2048];
	bool cancelled;
} interactive_reply_t;

/* 发送通用交互请求，让 Web UI 以指定 request_type 展示对应输入控件。 */
err_t channel_runtime_request_interactive(const struct message *msg,
					 const interactive_request_meta_t *meta);

/* 等待通用交互回复（阻塞等待，直到超时或收到 reply）。 */
bool channel_runtime_wait_interactive_reply(const struct message *msg,
					      const char *request_type,
					      const char *request_id,
					      interactive_reply_t *reply_out);

/* 向用户发送 sudo 密码请求（交互式提权） */
err_t channel_runtime_request_sudo(const struct message *msg,
					const char *request_id,
					const char *prompt_text);

/* 等待用户输入 sudo 密码（阻塞等待，直到超时或收到回复） */
bool channel_runtime_wait_sudo_password(const struct message *msg,
					const char *request_id,
					char *password_out,
					size_t password_out_size);

typedef err_t (*interactive_request_sender_fn_t)(const char *chat_id,
						 const char *request_type,
						 const char *request_id,
						 const char *prompt_text,
						 const char *task_id,
						 const char *session_id,
						 const char *coordinator_id);
typedef err_t (*interactive_sudo_sender_fn_t)(const char *chat_id,
					      const char *request_id,
					      const char *prompt_text,
					      const char *task_id,
					      const char *session_id,
					      const char *coordinator_id);

void interactive_set_sender_overrides_for_test(interactive_request_sender_fn_t interactive_sender,
					       interactive_sudo_sender_fn_t sudo_sender);
