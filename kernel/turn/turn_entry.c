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
#include "turn_gate.h"
#include "turn_context.h"
#include "turn_io.h"
#include "turn_pipeline.h"
#include "turn_prompt.h"
#include "turn_prepare.h"
#include "drivers/tool/tool_bus_view.h"
#include "bus.h"
#include "turn_common.h"
#include "linux/printk.h"

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
	if (latest) {
		bool lifecycle_prelude = delegate_frame_is_lifecycle_prelude(latest);

		text = cJSON_GetStringValue(cJSON_GetObjectItem(latest, "output_preview"));
		if ((!text || !text[0]) && !lifecycle_prelude) {
			text = cJSON_GetStringValue(cJSON_GetObjectItem(latest, "detail"));
		}
		if (text && text[0] &&
		    !(lifecycle_prelude &&
		      delegate_child_session_has_assistant_history(history))) {
			return delegate_render_visible_text(text, buf, buf_size);
		}
	}

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

			if (role && strcmp(role, "assistant") == 0 && content && content[0]) {
				return delegate_render_visible_text(content, buf, buf_size);
			}
			if (reasoning && reasoning[0]) {
				if (buf && buf_size > 0) {
					strscpy(buf, reasoning, buf_size);
					return buf;
				}
				return reasoning;
			}
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
	if (text && text[0]) {
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
	if (clean[0]) {
		strlcat(dst, "：", dst_size);
		strlcat(dst, clean, dst_size);
	}
	strlcat(dst, "\n", dst_size);
}

static void collect_delegate_next_files(char *dst,
					size_t dst_size,
					cJSON *agents)
{
	cJSON *agent = NULL;
	int appended = 0;

	cJSON_ArrayForEach(agent, agents) {
		char preferred[DELEGATE_COMPLETION_TEXT_BUF];
		const char *text = delegate_child_session_preferred_text(agent,
							 preferred,
							 sizeof(preferred));
		const char *marker;
		const char *cursor;

		if (!text || !text[0]) {
			continue;
		}
		marker = strstr(text, "建议继续看：");
		if (!marker) {
			continue;
		}
		cursor = marker + strlen("建议继续看：");
		while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
			cursor++;
		}
		if (!*cursor) {
			continue;
		}
		if (appended == 0) {
			strlcat(dst, "\n建议继续看：\n", dst_size);
		}
		strlcat(dst, "- ", dst_size);
		while (*cursor && *cursor != '\n' && *cursor != '\r') {
			char ch[2] = {*cursor++, '\0'};
			strlcat(dst, ch, dst_size);
		}
		strlcat(dst, "\n", dst_size);
		appended++;
		if (appended >= 3) {
			break;
		}
	}
}

static void append_delegate_relationship_summary(char *dst,
						 size_t dst_size,
						 cJSON *agents)
{
	cJSON *agent = NULL;
	bool saw_kernel = false;
	bool saw_tool = false;
	bool saw_llm = false;

	if (!dst || dst_size == 0 || !agents || !cJSON_IsArray(agents)) {
		return;
	}

	cJSON_ArrayForEach(agent, agents) {
		const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "description"));
		char preferred[DELEGATE_COMPLETION_TEXT_BUF];
		const char *text = delegate_child_session_preferred_text(agent,
							 preferred,
							 sizeof(preferred));

		if ((description && strstr(description, "kernel")) ||
		    (text && strstr(text, "kernel"))) {
			saw_kernel = true;
		}
		if ((description && (strstr(description, "tool") || strstr(description, "drivers/tool"))) ||
		    (text && (strstr(text, "drivers/tool") || strstr(text, "delegate_task") || strstr(text, "工具协议")))) {
			saw_tool = true;
		}
		if ((description && (strstr(description, "llm") || strstr(description, "drivers/llm"))) ||
		    (text && (strstr(text, "drivers/llm") || strstr(text, "provider") || strstr(text, "模型回退")))) {
			saw_llm = true;
		}
	}

	if (!(saw_kernel || saw_tool || saw_llm)) {
		return;
	}

	strlcat(dst, "模块关系：", dst_size);
	if (saw_kernel && saw_tool && saw_llm) {
		strlcat(dst,
			"`kernel` 是执行内核和主链调度中心，`drivers/tool` 承担工具协议与委托执行适配，`drivers/llm` 承担模型/provider 适配；整体关系是 kernel 通过 tool/llm 两层驱动外部能力。",
			dst_size);
	} else if (saw_kernel && saw_tool) {
		strlcat(dst,
			"`kernel` 负责主执行链，`drivers/tool` 负责把工具能力接到主链上，两者是内核与外部工具之间的调用边界。",
			dst_size);
	} else if (saw_kernel && saw_llm) {
		strlcat(dst,
			"`kernel` 负责主执行链，`drivers/llm` 提供统一的大模型调用适配，两者是执行内核与模型层之间的边界。",
			dst_size);
	} else if (saw_tool && saw_llm) {
		strlcat(dst,
			"`drivers/tool` 负责工具侧适配，`drivers/llm` 负责模型侧适配，两者共同为上层执行链提供外部能力入口。",
			dst_size);
	}
	strlcat(dst, "\n\n", dst_size);
}

static bool append_delegate_structured_boundary_summary(char *dst,
							size_t dst_size,
							cJSON *agents)
{
	cJSON *agent = NULL;
	bool saw_turn = false;
	bool saw_tooling = false;
	bool saw_tool = false;
	bool saw_llm = false;

	if (!dst || dst_size == 0 || !agents || !cJSON_IsArray(agents)) {
		return false;
	}

	cJSON_ArrayForEach(agent, agents) {
		const char *scope_path = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "scope_path"));
		const char *analysis_focus = cJSON_GetStringValue(cJSON_GetObjectItem(agent, "analysis_focus"));

		if ((scope_path && strstr(scope_path, "kernel/turn")) ||
		    (analysis_focus && strstr(analysis_focus, "turn_execution"))) {
			saw_turn = true;
		}
		if ((scope_path && strstr(scope_path, "kernel/tooling")) ||
		    (analysis_focus && strstr(analysis_focus, "coordination"))) {
			saw_tooling = true;
		}
		if ((scope_path && strstr(scope_path, "drivers/tool")) ||
		    (analysis_focus && strstr(analysis_focus, "tool_runtime"))) {
			saw_tool = true;
		}
		if ((scope_path && strstr(scope_path, "drivers/llm")) ||
		    (analysis_focus && strstr(analysis_focus, "llm_adapter"))) {
			saw_llm = true;
		}
	}

	if (!(saw_turn || saw_tooling || saw_tool || saw_llm)) {
		return false;
	}

	strlcat(dst, "职责边界：", dst_size);
	if (saw_turn && saw_tooling && saw_tool) {
		strlcat(dst,
			"`kernel/turn` 负责单回合执行主链和最终回复生成，`kernel/tooling` 负责后台协调、唤醒、工具治理与验证，`drivers/tool` 负责工具协议和运行时适配；调用方向应当是 turn 编排主链，tooling 管状态，tool 驱动外部工具能力。",
			dst_size);
		if (saw_llm) {
			strlcat(dst,
				" `drivers/llm` 则平行承担模型/provider 适配，为主链提供统一模型出口。",
				dst_size);
		}
	} else if (saw_turn && saw_tooling) {
		strlcat(dst,
			"`kernel/turn` 负责单回合执行主链、回合决策与最终回复生成，`kernel/tooling` 负责工具治理、后台协调、parent wake 和执行期验证；两者的边界应该是 turn 编排当前回合，tooling 提供回合外或跨回合的协调与治理能力，而不是反过来由 tooling 侵入主回复生成。",
			dst_size);
	} else if (saw_turn && saw_tool) {
		strlcat(dst,
			"`kernel/turn` 负责主链编排，`drivers/tool` 负责工具适配，二者边界是执行链与外部工具运行时之间的接口。",
			dst_size);
	} else if (saw_tooling && saw_tool) {
		strlcat(dst,
			"`kernel/tooling` 更偏协调与治理层，`drivers/tool` 更偏执行适配层，前者不应吞掉后者的运行时职责。",
			dst_size);
	} else if (saw_llm) {
		strlcat(dst,
			"`drivers/llm` 负责模型/provider 适配，与工具侧链路平行，为上层执行链提供统一模型出口。",
			dst_size);
	}
	strlcat(dst, "\n\n", dst_size);
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
	reply = calloc(1, 8192);
	if (!reply) {
		cJSON_Delete(root);
		return NULL;
	}

	snprintf(reply,
		 8192,
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
		 "状态：共 %d 个子任务，已完成 %d 个，未完成 %d 个。\n\n关键发现：\n",
		 agent_count, done_count, other_count);
	strlcat(reply, line, 8192);

	index = 0;
	cJSON_ArrayForEach(agent, agents) {
		append_delegate_agent_compact_line(reply, 8192, agent, index++);
	}

	strlcat(reply, "\n", 8192);
	if (!append_delegate_structured_boundary_summary(reply, 8192, agents)) {
		append_delegate_relationship_summary(reply, 8192, agents);
	}

	collect_delegate_next_files(reply, 8192, agents);

	strlcat(reply, "\n原始子任务摘要：\n\n", 8192);
	index = 0;
	cJSON_ArrayForEach(agent, agents) {
		append_delegate_agent_summary(reply, 8192, agent, index++);
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

static void agent_turn_run_from_prepared(struct message *msg,
					 agent_turn_io_t *io,
					 const agent_turn_decision_t *decision)
{
	const char *tools_json =
		strcmp(agent_msg_source_or_default(msg), MSG_SOURCE_DELEGATE) == 0
			? NULL
			: tool_bus_tools_json_for_channel(msg->channel);
	const char *model_override =
		agent_turn_resolve_model(msg, decision->active_role);

	agent_run_prepared_turn(msg, io->system_prompt, io->messages, tools_json,
				model_override, msg->chat_id, 0);
}

void agent_turn_process_new_message(struct message *msg)
{
	if (agent_turn_handle_self_test_command(msg)) {
		return;
	}

	if (agent_turn_validate_inbound_message(msg) != 0) {
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
