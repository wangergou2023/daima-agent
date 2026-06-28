/* 单回合入口前置处理：自检命令与内部控制过滤。 */
#include "turn_gate.h"

#include "turn_common.h"
#include "cjson.h"
#include "paths.h"
#include "workspace_probe.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

int agent_self_test(void);
char *agent_self_test_results_json(void);
void agent_self_test_set_log_probe(const self_test_log_probe_t *probe);
void agent_self_test_set_log_probe_pending(bool pending);

#define SELF_TEST_FOLLOWUP_SLOTS 8

typedef struct {
	bool active;
	char chat_id[128];
	char runtime_log_path[PATH_MAX];
	char log_marker[256];
} self_test_followup_slot_t;

static self_test_followup_slot_t s_self_test_followups[SELF_TEST_FOLLOWUP_SLOTS];

static void append_prompt_text(char *buf, size_t size, int *off, const char *text)
{
	int written;

	if (!buf || !size || !off || !text || *off < 0 || (size_t)*off >= size) {
		return;
	}
	written = snprintf(buf + *off, size - (size_t)*off, "%s", text);
	if (written < 0) {
		return;
	}
	if ((size_t)written >= size - (size_t)*off) {
		*off = (int)size - 1;
		return;
	}
	*off += written;
}

static int count_occurrences(const char *haystack, const char *needle)
{
	const char *cursor;
	int count = 0;

	if (!haystack || !needle || !needle[0]) {
		return 0;
	}

	cursor = haystack;
	while ((cursor = strstr(cursor, needle)) != NULL) {
		count++;
		cursor += strlen(needle);
	}
	return count;
}

bool agent_turn_build_self_test_followup_prompt(char *buf, size_t size,
						const char *analysis_root,
						const char *runtime_log_path,
						const char *log_marker)
{
	int prompt_off = 0;

	if (!buf || size == 0 || !analysis_root || !analysis_root[0] ||
	    !runtime_log_path || !runtime_log_path[0] ||
	    !log_marker || !log_marker[0]) {
		return false;
	}

	buf[0] = '\0';
	prompt_off += snprintf(
		buf + prompt_off, size - (size_t)prompt_off,
		"这是自检任务。你现在只分析 `~/.agent-data/spiffs_data/workspace/opencode`"
		"（当前解析为 %s）这一个仓库。"
		"进入分析前先确认：这里的 opencode 源码已经存在，而且是有效仓库；"
		"如果你看到的内容与这个目标路径不一致，直接报告自检环境异常，不要退回去分析整个 workspace。"
		"确认无误后，分析 %s 的目录结构和关键模块。"
		"必须同时安排多个 subagent，分别分析 packages/app、packages/cli、packages/session-ui，"
		"最后汇总它们的职责、边界和协作关系。"
		"先做委托，不要自己先大范围 files 摸底。"
		"完成仓库分析后，再读取日志 %s，判断这次自检里的 subagent 调度是否真的发生：",
		analysis_root,
		analysis_root,
		runtime_log_path);
	append_prompt_text(buf, size, &prompt_off,
			   "至少检查是否出现 delegate_store attach_task、delegate_bg launch candidate、");
	append_prompt_text(buf, size, &prompt_off,
			   "delegate_bg restore queued child 这些痕迹，并明确指出是多个子任务都启动了，");
	append_prompt_text(buf, size, &prompt_off,
			   "还是只有部分子任务真正跑起来。");
	append_prompt_text(buf, size, &prompt_off,
			   "读取日志时只统计这个 marker 之后的日志：");
	append_prompt_text(buf, size, &prompt_off, log_marker);
	append_prompt_text(buf, size, &prompt_off,
			   "。如果 marker 之后没有足够日志，也要明确报告日志证据不足。");
	append_prompt_text(buf, size, &prompt_off,
			   "最终结论分成两部分输出：1. opencode 目录结构与关键模块；");
	append_prompt_text(buf, size, &prompt_off,
			   "2. 基于日志的多 subagent 运行结论。");
	return prompt_off > 0 && (size_t)prompt_off < size;
}

bool agent_turn_build_self_test_log_marker(char *buf, size_t size,
					   const char *chat_id)
{
	if (!buf || size == 0 || !chat_id || !chat_id[0]) {
		return false;
	}

	buf[0] = '\0';
	snprintf(buf, size, "self_test_marker:%s:%lu",
		 chat_id,
		 (unsigned long)time(NULL));
	return buf[0] != '\0';
}

bool agent_turn_probe_self_test_runtime_log(const char *runtime_log_path,
					    const char *log_marker,
					    self_test_log_probe_t *probe)
{
	FILE *f;
	long size;
	char *buf = NULL;
	char *start;
	bool ok = false;

	if (!runtime_log_path || !runtime_log_path[0] || !log_marker || !log_marker[0] ||
	    !probe) {
		return false;
	}

	memset(probe, 0, sizeof(*probe));
	f = fopen(runtime_log_path, "r");
	if (!f) {
		return false;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return false;
	}
	size = ftell(f);
	if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}
	buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
		free(buf);
		fclose(f);
		return false;
	}
	buf[size] = '\0';
	fclose(f);

	start = strstr(buf, log_marker);
	if (!start) {
		free(buf);
		return true;
	}

	probe->marker_found = true;
	probe->attach_task_hits = count_occurrences(start, "delegate_store attach_task");
	probe->launch_candidate_hits = count_occurrences(start, "delegate_bg launch candidate");
	probe->restore_queued_hits = count_occurrences(start, "delegate_bg restore queued child");
	probe->multi_subagent_confirmed = probe->attach_task_hits >= 2 &&
		probe->launch_candidate_hits >= 2 &&
		probe->restore_queued_hits >= 1;
	ok = true;
	free(buf);
	return ok;
}

bool agent_turn_build_self_test_workspace_status(char *buf, size_t size,
						 const char *analysis_root,
						 bool repo_present_before,
						 bool repo_ready_after)
{
	const char *status_line;

	if (!buf || size == 0 || !analysis_root || !analysis_root[0]) {
		return false;
	}

	if (!repo_ready_after) {
		status_line = "状态：`workspace/opencode` 仍未准备好。";
	} else if (repo_present_before) {
		status_line = "状态：检测到现有 `workspace/opencode` 有效源码，直接复用。";
	} else {
		status_line = "状态：未发现现有 `workspace/opencode`，已自动 clone 一份 opencode 源码。";
	}

	buf[0] = '\0';
	snprintf(buf, size,
		 "🔎 自检工作区预检\n\n"
		 "%s\n"
		 "目标仓库：`~/.agent-data/spiffs_data/workspace/opencode`\n"
		 "实际路径：`%s`\n"
		 "下一步：基于这个仓库继续做多 subagent 分析，并回看 `agent.log` 判断调度是否真的发生。",
		 status_line,
		 analysis_root);
	return buf[0] != '\0';
}

static self_test_followup_slot_t *find_self_test_followup_slot(const char *chat_id)
{
	int free_idx = -1;

	if (!chat_id || !chat_id[0]) {
		return NULL;
	}

	for (int i = 0; i < SELF_TEST_FOLLOWUP_SLOTS; i++) {
		if (s_self_test_followups[i].active &&
		    strcmp(s_self_test_followups[i].chat_id, chat_id) == 0) {
			return &s_self_test_followups[i];
		}
		if (!s_self_test_followups[i].active && free_idx < 0) {
			free_idx = i;
		}
	}

	if (free_idx >= 0) {
		return &s_self_test_followups[free_idx];
	}
	return &s_self_test_followups[0];
}

void agent_turn_register_self_test_followup(const char *chat_id,
					    const char *runtime_log_path,
					    const char *log_marker)
{
	self_test_followup_slot_t *slot =
		find_self_test_followup_slot(chat_id);

	if (!slot || !runtime_log_path || !runtime_log_path[0] ||
	    !log_marker || !log_marker[0]) {
		return;
	}

	memset(slot, 0, sizeof(*slot));
	slot->active = true;
	strscpy(slot->chat_id, chat_id, sizeof(slot->chat_id));
	strscpy(slot->runtime_log_path, runtime_log_path,
		sizeof(slot->runtime_log_path));
	strscpy(slot->log_marker, log_marker, sizeof(slot->log_marker));
}

bool agent_turn_finalize_self_test_followup(const struct message *msg)
{
	self_test_followup_slot_t *slot;
	self_test_log_probe_t probe;
	struct message reply;
	char *json;

	if (!msg || !agent_msg_is_internal_control(msg) || !msg->chat_id[0]) {
		return false;
	}

	slot = find_self_test_followup_slot(msg->chat_id);
	if (!slot || !slot->active) {
		return false;
	}

	memset(&probe, 0, sizeof(probe));
	if (!agent_turn_probe_self_test_runtime_log(slot->runtime_log_path,
						    slot->log_marker,
						    &probe)) {
		memset(&probe, 0, sizeof(probe));
	}
	agent_self_test_set_log_probe(&probe);
	agent_self_test_set_log_probe_pending(false);
	slot->active = false;

	json = agent_self_test_results_json();
	if (!json) {
		return true;
	}

	memset(&reply, 0, sizeof(reply));
	strscpy(reply.channel, msg->channel, sizeof(reply.channel));
	strscpy(reply.chat_id, msg->chat_id, sizeof(reply.chat_id));
	reply.content = json;
	message_bus_push_outbound(&reply);
	return true;
}

bool agent_turn_handle_self_test_command(struct message *msg)
{
	if (!msg->content || strncmp(msg->content, "!test", 5) != 0) {
		return false;
	}

	agent_self_test();
	workspace_probe_repo_prepare_t prepare_result;
	char runtime_log_path[PATH_MAX];
	char log_marker[256];
	memset(&prepare_result, 0, sizeof(prepare_result));
	workspace_probe_prepare_opencode_repo(&prepare_result);
	snprintf(runtime_log_path, sizeof(runtime_log_path), "%s/agent.log",
		 path_memory_dir());
	if (!agent_turn_build_self_test_log_marker(log_marker, sizeof(log_marker),
						 msg->chat_id)) {
		strscpy(log_marker, "self_test_marker:unknown", sizeof(log_marker));
	}
	pr_info("%s", log_marker);
	agent_self_test_set_log_probe(NULL);
	agent_self_test_set_log_probe_pending(true);
	agent_turn_register_self_test_followup(msg->chat_id,
					       runtime_log_path,
					       log_marker);
	pr_info("self-test workspace probe: opencode present=%s ready=%s path=%s",
		prepare_result.repo_present_before ? "yes" : "no",
		prepare_result.repo_ready_after ? "yes" : "no",
		prepare_result.repo_path[0] ? prepare_result.repo_path : "<unresolved>");

	{
		char *json = agent_self_test_results_json();
		struct message reply;

		memset(&reply, 0, sizeof(reply));
		strscpy(reply.channel, msg->channel, sizeof(reply.channel));
		strscpy(reply.chat_id, msg->chat_id, sizeof(reply.chat_id));
		reply.content = json;
		message_bus_push_outbound(&reply);
	}

	if (!prepare_result.repo_ready_after) {
		struct message fail_reply;
		char fail_buf[1024];

		memset(&fail_reply, 0, sizeof(fail_reply));
		strscpy(fail_reply.channel, msg->channel, sizeof(fail_reply.channel));
		strscpy(fail_reply.chat_id, msg->chat_id, sizeof(fail_reply.chat_id));
		snprintf(fail_buf, sizeof(fail_buf),
			 "⚠️ 自检工作区准备失败\n\n"
			 "未能在 `~/.agent-data/spiffs_data/workspace/opencode` 准备好 opencode 源码，"
			 "自动 clone `https://github.com/sst/opencode.git` 也失败了。"
			 "这次不继续分析整个 workspace，请先排查 git/network/目录权限。");
		fail_reply.content = strdup(fail_buf);
		message_bus_push_outbound(&fail_reply);
		agent_cleanup_inbound_msg(msg);
		return true;
	}

	{
		struct message prep_reply;
		char prep_buf[1024];

		memset(&prep_reply, 0, sizeof(prep_reply));
		strscpy(prep_reply.channel, msg->channel, sizeof(prep_reply.channel));
		strscpy(prep_reply.chat_id, msg->chat_id, sizeof(prep_reply.chat_id));
		if (agent_turn_build_self_test_workspace_status(prep_buf,
								sizeof(prep_buf),
								prepare_result.repo_path,
								prepare_result.repo_present_before,
								prepare_result.repo_ready_after)) {
			prep_reply.content = strdup(prep_buf);
			message_bus_push_outbound(&prep_reply);
		}
	}

	struct message test_msg;
	memset(&test_msg, 0, sizeof(test_msg));
	strscpy(test_msg.channel, msg->channel, sizeof(test_msg.channel));
	strscpy(test_msg.chat_id, msg->chat_id, sizeof(test_msg.chat_id));
	strscpy(test_msg.source, "internal", sizeof(test_msg.source));
	char prompt[4096];
	const char *analysis_root = prepare_result.repo_path;

	if (!agent_turn_build_self_test_followup_prompt(prompt, sizeof(prompt),
							analysis_root,
							runtime_log_path,
							log_marker)) {
		struct message fail_reply;
		memset(&fail_reply, 0, sizeof(fail_reply));
		strscpy(fail_reply.channel, msg->channel, sizeof(fail_reply.channel));
		strscpy(fail_reply.chat_id, msg->chat_id, sizeof(fail_reply.chat_id));
		fail_reply.content = strdup("⚠️ 自检任务构造失败：follow-up prompt 生成异常。");
		message_bus_push_outbound(&fail_reply);
		agent_cleanup_inbound_msg(msg);
		return true;
	}
	test_msg.content = strdup(prompt);
	message_bus_push_inbound(&test_msg);

	agent_cleanup_inbound_msg(msg);
	return true;
}

err_t agent_turn_validate_inbound_message(struct message *msg)
{
	msg->intent = INTENT_OPEN;

	if (agent_msg_is_internal_control(msg)) {
		pr_info("Dropping internal control %s:%s", msg->channel, msg->chat_id);
		agent_cleanup_inbound_msg(msg);
		return ERR_FAIL;
	}

	return 0;
}
