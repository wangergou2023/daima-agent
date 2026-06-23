/* Turn prompt 构建：只负责 system prompt 注入链。 */

#include "turn_prompt_build.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "autoconf.h"
#include "channel_policy.h"
#include "compaction.h"
#include "context_build.h"
#include "debug.h"
#include "recovery.h"
#include "rules.h"
#include "todo.h"
#include "turn_common.h"
#include "drivers/memory/session_store.h"
#include "linux/kernel.h"

static void append_turn_context_prompt(char *prompt, size_t size, const struct message *msg)
{
	if (!prompt || size == 0 || !msg) {
		return;
	}

	size_t off = strnlen(prompt, size - 1);
	if (off >= size - 1) {
		return;
	}

	const char *source = agent_msg_source_or_default(msg);
	const char *kind = agent_msg_is_synthetic_event(msg) ? "系统触发事件" : "用户新消息";
	int n = snprintf(
		prompt + off, size - off,
		"\n## 当前轮运行时上下文\n\n"
		"### 当前消息\n"
		"- 来源通道: %s\n"
		"- 来源 chat_id: %s\n"
		"- 消息来源类型: %s\n"
		"- 当前消息性质: %s\n"
		"- 若本轮使用 cron action=add 发回当前会话，请设置 channel 与 chat_id 为来源值。\n",
		msg->channel[0] ? msg->channel : "(unknown)",
		msg->chat_id[0] ? msg->chat_id : "(empty)",
		source,
		kind);
	if (n < 0 || (size_t)n >= (size - off)) {
		prompt[size - 1] = '\0';
	}
}

#ifdef RULES_INJECTION_ENABLED
static void prepend_rules_prompt(char *prompt, size_t size, const char *rules)
{
	if (!prompt || size == 0 || !rules || !rules[0]) {
		return;
	}

	char existing[CONTEXT_BUF_SIZE];
	strscpy(existing, prompt, sizeof(existing));
	int n = snprintf(prompt, size, "%s\n%s", rules, existing);
	if (n < 0 || (size_t)n >= size) {
		prompt[size - 1] = '\0';
	}
}
#endif

static void append_session_facts_prompt(char *prompt, size_t size, const char *chat_id)
{
	if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
		return;
	}

	char facts_buf[2048];
	if (session_store_read_facts(chat_id, facts_buf, sizeof(facts_buf)) != 0 || !facts_buf[0]) {
		return;
	}

	size_t off = strnlen(prompt, size - 1);
	if (off >= size - 1) {
		return;
	}

	bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
	int n = snprintf(
		prompt + off, size - off,
		"%s### 稳定事实卡片\n"
		"以下内容是从更早轮次中提炼出的稳定偏好、约束、已确认决定。\n"
		"把它们当作长期有效的上下文；若与用户当前这轮明确新指令冲突，以当前新指令为准。\n\n"
		"%s\n",
		has_session_reference ? "\n" : "\n## 会话参考\n\n",
		facts_buf);

	if (n < 0 || (size_t)n >= (size - off)) {
		prompt[size - 1] = '\0';
	}
}

static void append_session_summary_prompt(char *prompt, size_t size, const char *chat_id)
{
	if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
		return;
	}

	char summary_buf[BUF_XLARGE];
	if (session_store_read_summary(chat_id, summary_buf, sizeof(summary_buf)) != 0 || !summary_buf[0]) {
		return;
	}

	size_t off = strnlen(prompt, size - 1);
	if (off >= size - 1) {
		return;
	}

	bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
	int n = snprintf(
		prompt + off, size - off,
		"%s### 最近一次上下文压缩摘要\n"
		"以下内容是对更早对话的结构化交接总结，用来帮助延续上下文。\n"
		"它不是新的用户输入；如果与当前这轮的明确要求冲突，以当前这轮为准。\n\n"
		"%s\n",
		has_session_reference ? "\n" : "\n## 会话参考\n\n",
		summary_buf);

	if (n < 0 || (size_t)n >= (size - off)) {
		prompt[size - 1] = '\0';
	}
}

err_t agent_turn_build_prompt(const struct message *msg,
			      const struct plan *plan,
			      char *system_prompt,
			      size_t system_prompt_size)
{
	if (!msg || !system_prompt || system_prompt_size == 0) {
		return ERR_INVALID_ARG;
	}

	char prompt_prefix[BUF_XLARGE] = {0};
	if (system_prompt[0]) {
		strscpy(prompt_prefix, system_prompt, sizeof(prompt_prefix));
	}

	context_build_system_prompt_for_channel(msg->channel, system_prompt, system_prompt_size);
	if (IS_ENABLED(CONFIG_RULES_INJECTION_ENABLED)) {
		char rules_buf[8192];
		if (rules_injection_load(rules_buf, sizeof(rules_buf)) == 0 && rules_buf[0]) {
			prepend_rules_prompt(system_prompt, system_prompt_size, rules_buf);
		}
	}
	append_session_summary_prompt(system_prompt, system_prompt_size, msg->chat_id);
	if (IS_ENABLED(CONFIG_COMPACTION_RECOVERY_ENABLED)) {
		compaction_recovery_inject(msg->chat_id, system_prompt, system_prompt_size);
	}
	if (IS_ENABLED(CONFIG_TODO_ENFORCER_ENABLED)) {
		todo_enforcer_inject_prompt(msg->chat_id, system_prompt, system_prompt_size);
	}
	if (IS_ENABLED(CONFIG_SESSION_RECOVERY_ENABLED)) {
		session_recovery_t rec = session_recovery_check(msg->chat_id);
		if (rec.has_crash) {
			session_recovery_inject_prompt(msg->chat_id, system_prompt, system_prompt_size);
			session_recovery_clear(msg->chat_id);
		}
	}
	append_session_facts_prompt(system_prompt, system_prompt_size, msg->chat_id);
	append_turn_context_prompt(system_prompt, system_prompt_size, msg);
	agent_channel_policy_append(system_prompt, system_prompt_size, msg);
	if (prompt_prefix[0]) {
		size_t off = strnlen(system_prompt, system_prompt_size - 1);
		if (off < system_prompt_size - 1) {
			int n = strscpy(system_prompt + off, prompt_prefix, system_prompt_size - off);
			if (n < 0 || (size_t)n >= system_prompt_size - off) {
				system_prompt[system_prompt_size - 1] = '\0';
			}
		}
	}
	if (plan && plan->has_plan) {
		(void)plan_review_inject_to_prompt(plan, system_prompt, system_prompt_size);
	}
	context_fix_truncated_utf8(system_prompt, strnlen(system_prompt, system_prompt_size));

	agent_prompt_dump_snapshot(msg, system_prompt);
	pr_info("LLM turn context: channel=%s chat_id=%s source=%s",
		msg->channel, msg->chat_id, agent_msg_source_or_default(msg));
	return 0;
}
