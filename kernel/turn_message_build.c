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
			"这是系统注入的定时提醒事件，不是用户刚刚发送的新消息。\n"
			"事件来源：cron\n"
			"处理要求：若提醒已到点，请直接自然地向用户发出提醒；"
			"不要把这段内容当成用户回复，也不要否认之前已经成功设置的提醒。\n\n"
			"提醒内容：%s";
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
			"这是系统触发的后台巡检事件，不是用户刚刚发送的新消息。\n"
			"事件来源：heartbeat\n"
			"请把下面内容当作系统任务说明执行；若无需用户感知，就不要假装这是用户在说话。\n\n"
			"任务内容：%s";
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
			"这是内部控制事件，不是用户消息。\n"
			"不要把它当成对话内容，也不要向用户复述任何内部载荷。");
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
				"用户发送了一张图片，但当前这次请求没有成功附带图片内容。不要臆测图片细节；请明确说明当前无法读取这张图片，并提示用户稍后重试。");
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
