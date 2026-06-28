/* 单回合主链入口：前置判断、决策生成、prepare/run/finish 串接。 */
#include "turn_entry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_delegate_protocol.h"
#include "drivers/tool/tool_delegate_result_json.h"
#include "delegate/delegate_session_json.h"
#include "turn_decision.h"
#include "turn_finish.h"
#include "turn_context.h"
#include "turn_io.h"
#include "turn_pipeline.h"
#include "turn_prompt.h"
#include "turn_prepare.h"
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_decomposition_policy.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/tool/tool_orchestration_policy.h"
#include "bus.h"
#include "turn_common.h"
#include "linux/printk.h"
#include "text.h"

static void agent_turn_finish_prepare_error(struct message *msg, err_t err)
{
	char *final_text = NULL;
	char *reasoning_text = NULL;

	agent_turn_finish(msg, &final_text, &reasoning_text, err, 0, false, false);
}

static const char *find_delegate_snapshot_json(const char *content)
{
	if (!content || !content[0]) {
		return NULL;
	}

	const char *marker = strstr(content, "Coordinator snapshot:\n");
	if (marker) {
		marker += strlen("Coordinator snapshot:\n");
		while (*marker == '\n' || *marker == '\r' || *marker == ' ' || *marker == '\t') {
			marker++;
		}
		return *marker == '{' ? marker : NULL;
	}

	const char *json = strchr(content, '{');
	return json;
}

#define DELEGATE_COMPLETION_TEXT_BUF 2048
#define DELEGATE_COMPLETION_RENDERED_BUF 2048
#define DELEGATE_COMPLETION_CLEAN_BUF 2048
#define DELEGATE_COMPLETION_LINE_BUF 2304

static bool delegate_child_session_has_assistant_history(cJSON *history)
{
	if (!history || !cJSON_IsArray(history)) {
		return false;
	}

	for (int idx = cJSON_GetArraySize(history) - 1; idx >= 0; idx--) {
		cJSON *entry = cJSON_GetArrayItem(history, idx);
		const char *role = entry
			? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "role"))
			: NULL;
		const char *content = entry
			? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "content"))
			: NULL;
		if (role && strcmp(role, "assistant") == 0 && content && content[0]) {
			return true;
		}
	}

	return false;
}

static bool delegate_text_looks_like_lifecycle_prelude(const char *text)
{
	if (!text || !text[0]) {
		return false;
	}

	return strstr(text, "· started") != NULL ||
	       strstr(text, "· running") != NULL ||
	       strcmp(text, "queued") == 0;
}

static bool delegate_is_compaction_summary_text(const char *text)
{
	return text &&
	       strncmp(text, "[上下文压缩摘要]", strlen("[上下文压缩摘要]")) == 0;
}

static bool delegate_text_is_preferred_final_text(const char *text)
{
	return text && text[0] &&
	       !delegate_text_looks_like_lifecycle_prelude(text) &&
	       !delegate_is_compaction_summary_text(text);
}

static void delegate_trim_copy(char *dst, size_t dst_size, const char *src)
{
	size_t len = 0;
	size_t start = 0;
	size_t end = 0;

	if (!dst || dst_size == 0) {
		return;
	}
	dst[0] = '\0';
	if (!src || !src[0]) {
		return;
	}

	strscpy(dst, src, dst_size);
	len = strlen(dst);
	while (start < len &&
	       (dst[start] == ' ' || dst[start] == '\t' ||
		dst[start] == '\n' || dst[start] == '\r')) {
		start++;
	}
	end = len;
	while (end > start &&
	       (dst[end - 1] == ' ' || dst[end - 1] == '\t' ||
		dst[end - 1] == '\n' || dst[end - 1] == '\r')) {
		end--;
	}
	if (start > 0) {
		memmove(dst, dst + start, end - start);
	}
	dst[end - start] = '\0';
}

static bool delegate_frame_is_lifecycle_prelude(cJSON *frame)
{
	const char *type = NULL;
	const char *phase = NULL;
	const char *status = NULL;

	if (!frame || !cJSON_IsObject(frame)) {
		return false;
	}

	type = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "type"));
	phase = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "phase"));
	status = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "status"));
	if (type && strcmp(type, "subagent_start") == 0) {
		return true;
	}
	if (type &&
	    (strcmp(type, "subagent_progress") == 0 || strcmp(type, "subagent_step") == 0) &&
	    phase && (strcmp(phase, "queued") == 0 || strcmp(phase, "progress") == 0) &&
	    status && (strcmp(status, "queued") == 0 || strcmp(status, "running") == 0)) {
		return true;
	}
	return false;
}

static const char *delegate_render_visible_text(const char *text,
						    char *buf,
						    size_t buf_size)
{
	if (!text || !text[0]) {
		return NULL;
	}
	if (buf && buf_size > 0 &&
	    tool_delegate_parse_result_json_rendered(text, buf, buf_size)) {
		return buf;
	}
	return text;
}

static const char *delegate_inline_child_session_preferred_text(cJSON *child,
								char *buf,
								size_t buf_size)
{
	cJSON *latest = NULL;
	cJSON *history = NULL;
	cJSON *commits = NULL;
	const char *text = NULL;

	if (!child || !cJSON_IsObject(child)) {
		return NULL;
	}

	latest = cJSON_GetObjectItem(child, "latest_frame");
	history = cJSON_GetObjectItem(child, "history");
	if (history && cJSON_IsArray(history)) {
		int size = cJSON_GetArraySize(history);
		for (int idx = size - 1; idx >= 0; idx--) {
			cJSON *entry = cJSON_GetArrayItem(history, idx);
			const char *role = entry
				? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "role"))
				: NULL;
			const char *content = entry
				? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "content"))
				: NULL;
			const char *reasoning = entry
				? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "reasoning"))
				: NULL;

			if (role && strcmp(role, "assistant") == 0 &&
			    delegate_text_is_preferred_final_text(content)) {
				return delegate_render_visible_text(content, buf, buf_size);
			}
			if (delegate_text_is_preferred_final_text(reasoning)) {
				if (buf && buf_size > 0) {
					strscpy(buf, reasoning, buf_size);
					return buf;
				}
				return reasoning;
			}
		}
	}

	if (latest) {
		bool lifecycle_prelude = delegate_frame_is_lifecycle_prelude(latest);

		text = cJSON_GetStringValue(cJSON_GetObjectItem(latest, "output_preview"));
		if (!delegate_text_is_preferred_final_text(text) && !lifecycle_prelude) {
			text = cJSON_GetStringValue(cJSON_GetObjectItem(latest, "detail"));
		}
		if (delegate_text_is_preferred_final_text(text) &&
		    !(lifecycle_prelude &&
		      delegate_child_session_has_assistant_history(history))) {
			return delegate_render_visible_text(text, buf, buf_size);
		}
	}

	commits = cJSON_GetObjectItem(child, "commits");
	if (commits && cJSON_IsArray(commits)) {
		int size = cJSON_GetArraySize(commits);
		for (int idx = size - 1; idx >= 0; idx--) {
			cJSON *entry = cJSON_GetArrayItem(commits, idx);
			const char *commit_text = entry
				? cJSON_GetStringValue(cJSON_GetObjectItem(entry, "text"))
				: NULL;
			if (commit_text && commit_text[0]) {
				return delegate_render_visible_text(commit_text, buf, buf_size);
			}
		}
	}

	text = cJSON_GetStringValue(cJSON_GetObjectItem(child, "summary"));
	if (delegate_text_is_preferred_final_text(text)) {
		return delegate_render_visible_text(text, buf, buf_size);
	}

	return NULL;
}

static const char *delegate_child_session_preferred_text(cJSON *agent,
						      char *buf,
						      size_t buf_size)
{
	delegate_task_record_t snapshot;
	cJSON *child = NULL;
	const char *summary = NULL;
	const char *output = NULL;
	const char *text = NULL;

	if (!agent) {
		return NULL;
	}

	memset(&snapshot, 0, sizeof(snapshot));
	summary = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "summary"));
	output = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "output"));
	child = cJSON_GetObjectItem(agent, "child_session");
	text = delegate_inline_child_session_preferred_text(child, buf, buf_size);
	if (text && text[0]) {
		return text;
	}

	if (buf && buf_size > 0) {
		const char *task_id = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "task_id"));
		if (task_id && task_id[0] &&
		    delegate_task_store_snapshot(task_id, &snapshot) == 0 &&
		    delegate_child_session_preferred_visible_text(&snapshot, buf, buf_size)) {
			return buf;
		}
	}

	if (summary && summary[0]) {
		return delegate_render_visible_text(summary, buf, buf_size);
	}
	return delegate_render_visible_text(output, buf, buf_size);
}

static void delegate_extract_summary_lead(const char *text,
					      char *lead,
					      size_t lead_size)
{
	const char *p = text;
	size_t n = 0;

	if (!lead || lead_size == 0) {
		return;
	}
	lead[0] = '\0';
	if (!text || !text[0]) {
		return;
	}

	while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t' ||
	       *p == '-' || *p == '*' || *p == '1' || *p == '2' || *p == '3' ||
	       *p == '4' || *p == '5' || *p == '6' || *p == '7' || *p == '8' ||
	       *p == '9' || *p == '.' || *p == ':') {
		p++;
	}

	while (*p && n + 1 < lead_size) {
		if (*p == '\n' || *p == '\r') {
			break;
		}
		lead[n++] = *p++;
		if (n >= 220 &&
		    (*p == '.' || *p == '!' || *p == '?' || *p == ';')) {
			lead[n++] = *p++;
			break;
		}
	}
	lead[n] = '\0';
	delegate_trim_copy(lead, lead_size, lead);
}

static void append_delegate_agent_summary(char *dst,
					      size_t dst_size,
					      cJSON *agent,
					      int index)
{
	char clean[DELEGATE_COMPLETION_CLEAN_BUF];
	char rendered[DELEGATE_COMPLETION_RENDERED_BUF];
	char line[DELEGATE_COMPLETION_LINE_BUF];
	char preferred[DELEGATE_COMPLETION_TEXT_BUF];
	const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description"));
	const char *subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "subagent_type"));
	const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "status"));
	const char *text = delegate_child_session_preferred_text(agent, preferred, sizeof(preferred));

	if (text && text[0] &&
	    tool_delegate_parse_result_json_rendered(text, rendered, sizeof(rendered))) {
		text = rendered;
	}

	clean[0] = '\0';
	if (text && text[0]) {
		tool_delegate_sanitize_summary_text_copy(clean, sizeof(clean), text);
	}

	snprintf(line, sizeof(line), "%d. %s",
		 index + 1,
		 description && description[0]
			 ? description
			 : (subagent_type && subagent_type[0] ? subagent_type : "子任务"));
	strlcat(dst, line, dst_size);

	if (status && status[0]) {
		snprintf(line, sizeof(line), " [%s]", status);
		strlcat(dst, line, dst_size);
	}
	strlcat(dst, "\n", dst_size);

	if (clean[0]) {
		strlcat(dst, clean, dst_size);
		strlcat(dst, "\n\n", dst_size);
	} else {
		strlcat(dst, "未返回可用摘要。\n\n", dst_size);
	}
}

static void append_delegate_agent_compact_line(char *dst,
					       size_t dst_size,
					       cJSON *agent,
					       int index)
{
	char clean[DELEGATE_COMPLETION_CLEAN_BUF];
	char lead[320];
	char preview[320];
	char rendered[DELEGATE_COMPLETION_RENDERED_BUF];
	char line[DELEGATE_COMPLETION_LINE_BUF];
	char preferred[DELEGATE_COMPLETION_TEXT_BUF];
	const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description"));
	const char *subagent_type = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "subagent_type"));
	const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "status"));
	const char *text = delegate_child_session_preferred_text(agent, preferred, sizeof(preferred));

	if (text && text[0] &&
	    tool_delegate_parse_result_json_rendered(text, rendered, sizeof(rendered))) {
		text = rendered;
	}

	clean[0] = '\0';
	if (text && text[0]) {
		tool_delegate_sanitize_summary_text_copy(clean, sizeof(clean), text);
	}
	preview[0] = '\0';
	if (clean[0]) {
		delegate_extract_summary_lead(clean, lead, sizeof(lead));
		if (lead[0]) {
			text_shorten(lead, preview, sizeof(preview), 220);
		} else {
			text_shorten(clean, preview, sizeof(preview), 220);
		}
	}

	snprintf(line, sizeof(line), "- %d. %s",
		 index + 1,
		 description && description[0]
			 ? description
			 : (subagent_type && subagent_type[0] ? subagent_type : "子任务"));
	strlcat(dst, line, dst_size);
	if (status && status[0]) {
		snprintf(line, sizeof(line), " [%s]", status);
		strlcat(dst, line, dst_size);
	}
	if (preview[0]) {
		strlcat(dst, "：", dst_size);
		strlcat(dst, preview, dst_size);
	}
	strlcat(dst, "\n", dst_size);
}

static bool append_delegate_observed_scope_summary(char *dst,
						   size_t dst_size,
						   cJSON *agents)
{
	cJSON *agent = NULL;
	char scopes[8][160];
	int scope_count = 0;

	if (!dst || dst_size == 0 || !agents || !cJSON_IsArray(agents)) {
		return false;
	}

	cJSON_ArrayForEach(agent, agents) {
		const char *scope_path = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "scope_path"));
		bool seen = false;

		if (!scope_path || !scope_path[0]) {
			continue;
		}
		for (int i = 0; i < scope_count; i++) {
			if (strcmp(scopes[i], scope_path) == 0) {
				seen = true;
				break;
			}
		}
		if (seen || scope_count >= ARRAY_SIZE(scopes)) {
			continue;
		}
		strscpy(scopes[scope_count++], scope_path, sizeof(scopes[0]));
	}

	if (scope_count == 0) {
		return false;
	}

	strlcat(dst, "覆盖范围：", dst_size);
	for (int i = 0; i < scope_count; i++) {
		if (i > 0) {
			strlcat(dst, "、", dst_size);
		}
		strlcat(dst, "`", dst_size);
		strlcat(dst, scopes[i], dst_size);
		strlcat(dst, "`", dst_size);
	}
	strlcat(dst, "。\n\n",
		dst_size);
	return true;
}

static char *build_delegate_completion_reply(const struct message *msg)
{
	cJSON *root;
	cJSON *agents;
	const char *json_text;
	char *reply;
	int agent_count = 0;
	int done_count = 0;
	int other_count = 0;

	json_text = find_delegate_snapshot_json(msg ? msg->content : NULL);
	if (!json_text) {
		return NULL;
	}

	root = cJSON_Parse(json_text);
	if (!root) {
		return NULL;
	}

	agents = cJSON_GetObjectItem(root, "agents");
	if (!agents || !cJSON_IsArray(agents)) {
		cJSON_Delete(root);
		return NULL;
	}

	agent_count = cJSON_GetArraySize(agents);
	reply = calloc(1, 16384);
	if (!reply) {
		cJSON_Delete(root);
		return NULL;
	}

	snprintf(reply,
		 16384,
		 "并行子任务汇总\n\n");

	cJSON *agent = NULL;
	int index = 0;
	cJSON_ArrayForEach(agent, agents) {
		const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "status"));
		if (status && strcmp(status, "done") == 0) {
			done_count++;
		} else {
			other_count++;
		}
		index++;
	}

	char line[256];
	snprintf(line, sizeof(line),
		 "状态：共 %d 个子任务，已完成 %d 个，未完成 %d 个。\n\n子任务结论：\n",
		 agent_count, done_count, other_count);
	strlcat(reply, line, 16384);

	index = 0;
	cJSON_ArrayForEach(agent, agents) {
		append_delegate_agent_compact_line(reply, 16384, agent, index++);
	}

	strlcat(reply, "\n", 16384);
	append_delegate_observed_scope_summary(reply, 16384, agents);

	strlcat(reply, "详细子任务结果：\n\n", 16384);
	index = 0;
	cJSON_ArrayForEach(agent, agents) {
		append_delegate_agent_summary(reply, 16384, agent, index++);
	}

	if (reply[0] == '\0') {
		free(reply);
		reply = NULL;
	}
	cJSON_Delete(root);
	return reply;
}

static bool agent_turn_try_finish_delegate_completion(struct message *msg)
{
	char *final_text = NULL;
	char *reasoning_text = NULL;
	cJSON *root = NULL;
	const char *json_text = NULL;
	const char *coordinator_id = NULL;
	unsigned long visible_revision = 0;

	if (!msg || strcmp(agent_msg_source_or_default(msg), MSG_SOURCE_DELEGATE) != 0) {
		return false;
	}

	json_text = find_delegate_snapshot_json(msg->content);
	if (json_text) {
		root = cJSON_Parse(json_text);
		if (root) {
			coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
			cJSON *visible_item = cJSON_GetObjectItem(root, "visible_revision");
			if (cJSON_IsNumber(visible_item) && visible_item->valuedouble > 0) {
				visible_revision = (unsigned long)visible_item->valuedouble;
			}
		}
	}

	if (msg->chat_id[0] && coordinator_id && coordinator_id[0] && visible_revision > 0) {
		(void)turn_context_set_delegate_resume_consumed(msg->chat_id,
								coordinator_id,
								visible_revision);
	}

	final_text = build_delegate_completion_reply(msg);
	if (!final_text || !final_text[0]) {
		cJSON_Delete(root);
		free(final_text);
		return false;
	}

	agent_turn_finish(msg, &final_text, &reasoning_text, 0, 0, false, false);
	cJSON_Delete(root);
	return true;
}

static bool message_explicitly_disallows_multi_subagents(const struct message *msg)
{
	const char *content;

	if (!msg || !msg->content) {
		return false;
	}

	content = msg->content;
	return strstr(content, "不要并行") != NULL ||
	       strstr(content, "不用并行") != NULL ||
	       strstr(content, "不要安排多个subagent") != NULL ||
	       strstr(content, "不要安排多个 subagent") != NULL ||
	       strstr(content, "不要多个subagent") != NULL ||
	       strstr(content, "不要多个 subagent") != NULL ||
	       strstr(content, "不要多个子代理") != NULL ||
	       strstr(content, "不要拆分") != NULL ||
	       strstr(content, "do not parallel") != NULL ||
	       strstr(content, "do not use multiple subagents") != NULL ||
	       strstr(content, "don't use multiple subagents") != NULL;
}

static bool turn_should_expose_delegate_tool(const struct message *msg)
{
	if (!msg) {
		return false;
	}
	if (strcmp(agent_msg_source_or_default(msg), MSG_SOURCE_DELEGATE) == 0) {
		return false;
	}
	if (message_explicitly_disallows_multi_subagents(msg)) {
		return false;
	}
	if (tool_invocation_context_message_should_offer_delegate_tool(msg)) {
		return true;
	}
	return false;
}

static void agent_turn_run_from_prepared(struct message *msg,
					 agent_turn_io_t *io,
					 const agent_turn_decision_t *decision)
{
	tool_decomposition_mode_t decomp_mode =
		tool_decomposition_policy_classify_message(msg);
	bool delegate_only = tool_orchestration_policy_requires_delegate_only(msg);
	const char *tools_json =
		delegate_only
			? tool_bus_tools_json_delegate_only()
			: (turn_should_expose_delegate_tool(msg)
				? tool_bus_tools_json_for_channel(msg->channel)
				: tool_bus_tools_json_for_channel_without_delegate(msg->channel));
	const char *model_override =
		agent_turn_resolve_model(msg, decision->active_role);

	pr_info("turn tool routing: chat=%s mode=%s delegate_only=%d expose_delegate=%d",
		msg && msg->chat_id[0] ? msg->chat_id : "-",
		tool_decomposition_mode_name(decomp_mode),
		delegate_only ? 1 : 0,
		turn_should_expose_delegate_tool(msg) ? 1 : 0);

	agent_run_prepared_turn(msg, io->system_prompt, io->messages, tools_json,
				model_override, msg->chat_id, 0);
}

void agent_turn_process_new_message(struct message *msg)
{
	if (agent_msg_is_internal_control(msg)) {
		pr_info("Dropping internal control %s:%s", msg->channel, msg->chat_id);
		agent_cleanup_inbound_msg(msg);
		return;
	}

	if (agent_turn_try_finish_delegate_completion(msg)) {
		return;
	}

	agent_turn_io_t io = {0};
	if (!agent_turn_io_init(&io)) {
		return;
	}

	agent_turn_decision_t decision;
	agent_turn_decision_reset(&decision);
	agent_turn_decide(msg, &decision);
	pr_info("TurnEntry decision: chat=%s channel=%s source=%s intent=%s content=%.160s",
		msg->chat_id,
		msg->channel,
		agent_msg_source_or_default(msg),
		intent_name(msg->intent),
		msg->content ? msg->content : "");

	err_t err = agent_turn_prepare(msg,
				       io.system_prompt, AGENT_TURN_IO_BUF_SIZE,
				       io.history_json, AGENT_TURN_IO_BUF_SIZE,
				       &io.messages);
	if (err == 0) {
		agent_turn_append_role_prompt(io.system_prompt,
					      AGENT_TURN_IO_BUF_SIZE,
					      decision.active_role);
	}

	if (err == 0) {
		agent_turn_run_from_prepared(msg, &io, &decision);
	} else {
		agent_turn_finish_prepare_error(msg, err);
	}

	agent_turn_io_cleanup(&io);
}
