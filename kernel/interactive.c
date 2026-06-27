/* 交互模式：通用 interactive request/reply 与 sudo 密码等待机制。 */

#include "interactive.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drivers/channel/gateway/ws_server.h"
#include "delegate/delegate_task_store.h"
#include "linux/slab.h"

static interactive_request_sender_fn_t s_interactive_sender_override = NULL;
static interactive_sudo_sender_fn_t s_sudo_sender_override = NULL;

static err_t send_interactive_request_to_chat(const char *chat_id,
					      const interactive_request_meta_t *meta,
					      const char *task_id,
					      const char *session_id,
					      const char *coordinator_id)
{
	if (!chat_id || !chat_id[0] || !meta || !meta->request_type || !meta->request_type[0] ||
	    !meta->request_id || !meta->request_id[0]) {
		return ERR_INVALID_ARG;
	}
	if (s_interactive_sender_override) {
		return s_interactive_sender_override(chat_id,
						      meta->request_type,
						      meta->request_id,
						      meta->prompt_text ? meta->prompt_text : "",
						      task_id ? task_id : "",
						      session_id ? session_id : "",
						      coordinator_id ? coordinator_id : "");
	}
	return ws_server_send_interactive_request(chat_id,
						 meta->request_type,
						 meta->request_id,
						 meta->prompt_text ? meta->prompt_text : "",
						 task_id ? task_id : "",
						 session_id ? session_id : "",
						 coordinator_id ? coordinator_id : "");
}

static err_t send_sudo_request_to_chat(const char *chat_id,
				       const char *request_id,
				       const char *prompt_text,
				       const char *task_id,
				       const char *session_id,
				       const char *coordinator_id)
{
	if (!chat_id || !chat_id[0] || !request_id || !request_id[0]) {
		return ERR_INVALID_ARG;
	}
	if (s_sudo_sender_override) {
		return s_sudo_sender_override(chat_id,
					      request_id,
					      prompt_text ? prompt_text : "",
					      task_id ? task_id : "",
					      session_id ? session_id : "",
					      coordinator_id ? coordinator_id : "");
	}
	return ws_server_send_sudo_request(chat_id,
					   request_id,
					   prompt_text ? prompt_text : "",
					   task_id ? task_id : "",
					   session_id ? session_id : "",
					   coordinator_id ? coordinator_id : "");
}

static bool interactive_resolve_parent_route(const struct message *msg,
					     delegate_parent_route_view_t *route_out)
{
	if (!msg || !route_out) {
		return false;
	}

	memset(route_out, 0, sizeof(*route_out));
	if (strncmp(msg->chat_id, "delegate_sync_", 14) != 0) {
		return false;
	}

	if (delegate_task_store_find_parent_route_by_session(msg->chat_id, route_out) != 0) {
		memset(route_out, 0, sizeof(*route_out));
		return false;
	}
	return route_out->parent_chat_id[0] != '\0';
}

err_t channel_runtime_request_interactive(const struct message *msg,
					 const interactive_request_meta_t *meta)
{
	delegate_parent_route_view_t route = {0};
	err_t err = 0;

	if (!msg || !meta) {
		return ERR_INVALID_ARG;
	}
	if (strcmp(msg->channel, CHAN_WEBSOCKET) != 0) {
		return ERR_FAIL;
	}

	if (interactive_resolve_parent_route(msg, &route)) {
		delegate_task_store_set_pending_request(route.task_id,
							meta->request_type,
							meta->request_id,
							meta->prompt_text ? meta->prompt_text : "");
		err = send_interactive_request_to_chat(route.parent_chat_id,
						       meta,
						       route.task_id,
						       route.session_id,
						       route.coordinator_id);
		return err;
	}
	return send_interactive_request_to_chat(msg->chat_id, meta, "", "", "");
}

static bool parse_interactive_reply(const char *payload,
				     const char *request_type,
				     const char *request_id,
				     interactive_reply_t *reply_out)
{
	if (!payload || !request_type || !request_id || !reply_out) {
		return false;
	}
	const char *prefix = "__interactive_reply__:";
	size_t prefix_len = strlen(prefix);
	if (strncmp(payload, prefix, prefix_len) != 0) {
		return false;
	}

	const char *rest = payload + prefix_len;
	const char *sep1 = strchr(rest, ':');
	const char *sep2 = sep1 ? strchr(sep1 + 1, ':') : NULL;
	const char *sep3 = sep2 ? strrchr(sep2 + 1, ':') : NULL;
	if (!sep1 || !sep2 || !sep3 || sep3 <= sep2) {
		return false;
	}

	size_t type_len = (size_t)(sep1 - rest);
	size_t rid_len = (size_t)(sep2 - (sep1 + 1));
	if (strlen(request_type) != type_len || strncmp(rest, request_type, type_len) != 0) {
		return false;
	}
	if (strlen(request_id) != rid_len || strncmp(sep1 + 1, request_id, rid_len) != 0) {
		return false;
	}

	size_t value_len = (size_t)(sep3 - (sep2 + 1));
	if (value_len >= sizeof(reply_out->value)) value_len = sizeof(reply_out->value) - 1;
	memcpy(reply_out->value, sep2 + 1, value_len);
	reply_out->value[value_len] = '\0';
	reply_out->cancelled = (*(sep3 + 1) == '1');
	return true;
}

bool channel_runtime_wait_interactive_reply(const struct message *msg,
					      const char *request_type,
					      const char *request_id,
					      interactive_reply_t *reply_out)
{
	if (!msg || !request_type || !request_type[0] || !request_id || !request_id[0] || !reply_out) {
		return false;
	}

	memset(reply_out, 0, sizeof(*reply_out));
	time_t deadline = time(NULL) + 180;
	struct message deferred[16];
	int deferred_count = 0;
	bool got_reply = false;
	delegate_parent_route_view_t route = {0};
	bool routed_to_parent = false;

	routed_to_parent = interactive_resolve_parent_route(msg, &route);

	while (time(NULL) < deadline) {
		struct message incoming = {0};
		err_t err = message_bus_pop_inbound(&incoming, 1000);
		if (err != 0) {
			continue;
		}

		bool same_channel = strcmp(incoming.channel, msg->channel) == 0;
		bool same_chat = strcmp(incoming.chat_id, msg->chat_id) == 0;
		bool same_parent_chat = routed_to_parent && route.parent_chat_id[0] &&
					strcmp(incoming.chat_id, route.parent_chat_id) == 0;
		if (same_channel && (same_chat || same_parent_chat) &&
		    parse_interactive_reply(incoming.content, request_type, request_id, reply_out)) {
			kfree(incoming.content);
			kfree(incoming.image_path);
			got_reply = true;
			break;
		}

		if (deferred_count < (int)(sizeof(deferred) / sizeof(deferred[0]))) {
			deferred[deferred_count++] = incoming;
		} else {
			message_bus_push_inbound(&incoming);
			usleep(50 * 1000);
		}
	}

	for (int i = 0; i < deferred_count; i++) {
		message_bus_push_inbound(&deferred[i]);
	}
	return got_reply;
}

err_t channel_runtime_request_sudo(const struct message *msg,
					const char *request_id,
					const char *prompt_text)
{
	delegate_parent_route_view_t route = {0};
	err_t err = 0;

	if (!msg || !request_id || !request_id[0]) {
		return ERR_INVALID_ARG;
	}
	if (strcmp(msg->channel, CHAN_WEBSOCKET) != 0) {
		return ERR_FAIL;
	}

	if (interactive_resolve_parent_route(msg, &route)) {
		delegate_task_store_set_pending_request(route.task_id,
							"sudo_password",
							request_id,
							prompt_text ? prompt_text : "");
		err = send_sudo_request_to_chat(route.parent_chat_id,
						request_id,
						prompt_text,
						route.task_id,
						route.session_id,
						route.coordinator_id);
		return err;
	}
	return send_sudo_request_to_chat(msg->chat_id, request_id, prompt_text, "", "", "");
}

bool channel_runtime_wait_sudo_password(const struct message *msg,
					const char *request_id,
					char *password_out,
					size_t password_out_size)
{
	if (!msg || !request_id || !password_out || password_out_size == 0) {
		return false;
	}
	if (channel_runtime_request_sudo(
		    msg,
		    request_id,
		    "This command requires sudo privileges. Please enter your sudo password to continue.") != 0) {
		return false;
	}

	interactive_reply_t reply = {0};
	if (!channel_runtime_wait_interactive_reply(msg, "sudo_password", request_id, &reply) ||
	    reply.cancelled || !reply.value[0]) {
		return false;
	}

	strncpy(password_out, reply.value, password_out_size - 1);
	password_out[password_out_size - 1] = '\0';
	return true;
}

void interactive_set_sender_overrides_for_test(interactive_request_sender_fn_t interactive_sender,
					       interactive_sudo_sender_fn_t sudo_sender)
{
	s_interactive_sender_override = interactive_sender;
	s_sudo_sender_override = sudo_sender;
}
