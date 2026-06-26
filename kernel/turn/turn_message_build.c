/* Turn messages 组装：只负责 history 和当前轮 messages。 */

#include "turn_message_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "autoconf.h"
#include "env.h"
#include "turn_common.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "linux/kernel.h"
#ifdef ENABLE_VISION
#include "drivers/vision/vision_capture.h"
#include "linux/slab.h"
#endif

static char *build_current_turn_content(const struct message *msg)
{
	const char *source = agent_msg_source_or_default(msg);
	const char *content = (msg && msg->content) ? msg->content : "";

	if (!agent_msg_is_synthetic_event(msg)) {
		return strdup(content);
	}

	if (strcmp(source, MSG_SOURCE_CRON) == 0) {
		const char *fmt =
			"This is a system-injected scheduled reminder event, not a new user message.\n"
			"Event source: cron\n"
			"Handling requirement: if the reminder is due, send the reminder to the user naturally.\n"
			"Do not treat this as a user reply, and do not deny an already-set reminder.\n\n"
			"Reminder payload: %s";
		size_t need = snprintf(NULL, 0, fmt, content) + 1;
		char *buf = kzalloc(need, GFP_KERNEL);
		if (!buf) {
			return NULL;
		}
		snprintf(buf, need, fmt, content);
		return buf;
	}

	if (strcmp(source, MSG_SOURCE_HEARTBEAT) == 0) {
		const char *fmt =
			"This is a system-triggered background inspection event, not a new user message.\n"
			"Event source: heartbeat\n"
			"Treat the following content as a system task description.\n"
			"If the user does not need to notice it, do not pretend the user said it.\n\n"
			"Task payload: %s";
		size_t need = snprintf(NULL, 0, fmt, content) + 1;
		char *buf = kzalloc(need, GFP_KERNEL);
		if (!buf) {
			return NULL;
		}
		snprintf(buf, need, fmt, content);
		return buf;
	}

	if (strcmp(source, MSG_SOURCE_INTERNAL) == 0) {
		return strdup(
			"This is an internal control event, not a user message.\n"
			"Do not treat it as conversation content and do not reveal any internal payload to the user.");
	}

	return strdup(content);
}

#ifdef ENABLE_VISION
static cJSON *build_user_vision_content(const char *text, const char *image_path)
{
	char local_path[256] = {0};
	bool cleanup_local_path = false;

	if (image_path && image_path[0]) {
		strscpy(local_path, image_path, sizeof(local_path));
	} else {
#ifdef BUILD_FOR_MIPS
		err_t cap_err = vision_capture_jpeg(NULL, local_path, sizeof(local_path));
		if (cap_err != 0) {
			return NULL;
		}
		cleanup_local_path = true;
#else
		(void)text;
		return NULL;
#endif
	}

	llm_image_content_t img = {0};
	err_t read_err = llm_image_read_file(local_path, &img);
	if (read_err != 0) {
		pr_warn("Failed to read image for multimodal request: %s (%s)",
			local_path, err_name(read_err));
		if (cleanup_local_path) {
			unlink(local_path);
		}
		return NULL;
	}

	cJSON *content = llm_create_multimodal_content(text, &img, 1);
	llm_image_content_free(&img);
	pr_info("Attached image to multimodal request: %s", local_path);

	if (cleanup_local_path) {
		const char *keep = env_get("VISION_KEEP_SNAPSHOT");
		if (!keep || !keep[0]) {
			unlink(local_path);
		}
	}

	return content;
}
#endif

err_t agent_turn_build_messages(const struct message *msg,
				char *history_json,
				size_t history_json_size,
				cJSON **out_messages)
{
	if (!msg || !history_json || history_json_size == 0 || !out_messages) {
		return ERR_INVALID_ARG;
	}

	*out_messages = NULL;

	if (history_json[0]) {
		/* 已有预加载的历史，跳过 session_store_get_history_json */
	} else {
		session_store_get_history_json(msg->chat_id, history_json, history_json_size, AGENT_MAX_HISTORY);
	}

	cJSON *messages = cJSON_Parse(history_json);
	if (!messages) {
		messages = cJSON_CreateArray();
	}
	if (!messages) {
		return ERR_NO_MEM;
	}

	cJSON *turn_msg = cJSON_CreateObject();
	if (!turn_msg) {
		cJSON_Delete(messages);
		return ERR_NO_MEM;
	}

	const char *role = agent_msg_role_for_current_turn(msg);
	char *current_content = build_current_turn_content(msg);
	if (!current_content) {
		cJSON_Delete(turn_msg);
		cJSON_Delete(messages);
		return ERR_NO_MEM;
	}

	cJSON_AddStringToObject(turn_msg, "role", role);
#ifdef ENABLE_VISION
	if (strcmp(role, "user") == 0) {
		cJSON *vision_content = build_user_vision_content(msg->content, msg->image_path);
		if (vision_content) {
			cJSON_AddItemToObject(turn_msg, "content", vision_content);
		} else if (msg->image_path && msg->image_path[0]) {
			cJSON_AddStringToObject(
				turn_msg,
				"content",
				"The user sent an image, but this request did not attach readable image content successfully. Do not guess image details; clearly say the image could not be read and ask the user to retry later.");
		} else {
			cJSON_AddStringToObject(turn_msg, "content", current_content);
		}
	} else {
		cJSON_AddStringToObject(turn_msg, "content", current_content);
	}
#else
	cJSON_AddStringToObject(turn_msg, "content", current_content);
#endif
	kfree(current_content);
	cJSON_AddItemToArray(messages, turn_msg);

	*out_messages = messages;
	return 0;
}
