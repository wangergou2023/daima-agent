/* 扩展钩子系统实现。
 * 管理全局钩子注册表（s_extensions[]），提供注册和触发接口。
 * 5 个生命周期钩子按注册顺序串行调用，首个非 0 返回值中断链。
 * replace_run 钩子特殊：任一返回 0 即替换 LLM 调用，不再继续。 */

#include "hooks.h"

/* 全局钩子注册表：静态数组，最大 AGENT_MAX_EXTENSIONS 个 */
static agent_extension_hooks_t *s_extensions[AGENT_MAX_EXTENSIONS];
static size_t s_extension_count;	/* 当前已注册扩展数 */
static bool s_initialized;		/* 是否已初始化 */

void agent_hooks_init(void)
{
#ifdef AGENT_HOOKS_TEST_RESET
	/* 测试模式：强制重置注册表 */
	s_extension_count = 0;
	for (size_t i = 0; i < AGENT_MAX_EXTENSIONS; i++) {
		s_extensions[i] = NULL;
	}
#endif
	s_initialized = true;
}

void agent_hooks_register(agent_extension_hooks_t *hooks)
{
	if (!hooks) {
		return;		/* 忽略 NULL 钩子 */
	}
	if (s_extension_count >= AGENT_MAX_EXTENSIONS) {
		return;		/* 注册表已满，静默丢弃 */
	}
	s_extensions[s_extension_count++] = hooks;	/* 追加到注册表末尾 */
	if (!s_initialized) {
		s_initialized = true;	/* 隐式初始化（兼容未显式调用 init 的场景） */
	}
}

/* 检查扩展是否既存在又已启用 */
static bool extension_enabled(const agent_extension_hooks_t *hooks)
{
	return hooks && hooks->enabled;
}

/* 触发 intent 钩子链：每个钩子可修改消息的意图 */
err_t agent_hooks_trigger_intent(struct message *msg)
{
	for (size_t i = 0; i < s_extension_count; i++) {
		agent_extension_hooks_t *hooks = s_extensions[i];
		if (!extension_enabled(hooks) || !hooks->on_intent) continue;
		err_t err = hooks->on_intent(msg);
		if (err != 0) return err;	/* 首个失败即中断 */
	}
	return 0;
}

/* 触发 prepare 钩子链：每个钩子可修改 system prompt 和 messages */
err_t agent_hooks_trigger_prepare(struct message *msg,
	char *system_prompt, size_t system_prompt_size, cJSON *messages)
{
	for (size_t i = 0; i < s_extension_count; i++) {
		agent_extension_hooks_t *hooks = s_extensions[i];
		if (!extension_enabled(hooks) || !hooks->on_prepare) continue;
		err_t err = hooks->on_prepare(msg, system_prompt, system_prompt_size, messages);
		if (err != 0) return err;
	}
	return 0;
}

/* 触发 before_run 钩子链：可覆盖模型选择和工具 */
err_t agent_hooks_trigger_before_run(struct message *msg,
	const char **model_override, const char *tools_json)
{
	for (size_t i = 0; i < s_extension_count; i++) {
		agent_extension_hooks_t *hooks = s_extensions[i];
		if (!extension_enabled(hooks) || !hooks->before_run) continue;
		err_t err = hooks->before_run(msg, model_override, tools_json);
		if (err != 0) return err;
	}
	return 0;
}

/* 触发 replace_run 钩子链：第一个成功（返回 0）的钩子替换 LLM 调用 */
err_t agent_hooks_trigger_replace_run(struct message *msg,
	char *system_prompt, cJSON *messages, const char *tools_json,
	char **out_final_text)
{
	for (size_t i = 0; i < s_extension_count; i++) {
		agent_extension_hooks_t *hooks = s_extensions[i];
		if (!extension_enabled(hooks) || !hooks->replace_run) continue;
		err_t err = hooks->replace_run(msg, system_prompt, messages, tools_json, out_final_text);
		if (err == 0) return 0;	/* 首个成功即返回 */
	}
	return ERR_FAIL;	/* 所有钩子都失败 => 继续正常 LLM 调用 */
}

/* 触发 finish 钩子链：不中断，所有已启用的钩子都会被调用 */
void agent_hooks_trigger_finish(struct message *msg, const char *response)
{
	for (size_t i = 0; i < s_extension_count; i++) {
		agent_extension_hooks_t *hooks = s_extensions[i];
		if (!extension_enabled(hooks) || !hooks->on_finish) continue;
		hooks->on_finish(msg, response);	/* finish 钩子无返回值，全部执行 */
	}
}
