/* 扩展钩子系统接口。
 * 提供 5 个生命周期钩子点，允许外部模块（extensions/）注册回调函数，
 * 在 Agent turn 流水线的关键阶段注入自定义行为。
 * 钩子链按注册顺序串行执行，任一钩子返回错误则中断后续钩子。 */

#pragma once

#include "turn_common.h"
#include "err.h"
#include "cjson.h"

#include <stdbool.h>
#include <stddef.h>

/* 扩展基本信息 */
typedef struct {
	bool enabled;		/* 是否启用此扩展 */
	const char *name;	/* 扩展名称（用于日志/调试） */
} agent_extension_t;

/* 钩子函数类型定义 — 对应 5 个生命周期阶段 */

/* on_intent: 意图识别后调用，可修改消息的 intent 字段 */
typedef err_t (*agent_hook_on_intent_fn)(struct message *msg);

/* on_prepare: Turn 准备阶段，可修改 system prompt 和对话历史 */
typedef err_t (*agent_hook_on_prepare_fn)(struct message *msg,
	char *system_prompt, size_t system_prompt_size, cJSON *messages);

/* before_run: LLM 调用前，可覆盖模型选择和工具定义 */
typedef err_t (*agent_hook_before_run_fn)(struct message *msg,
	const char **model_override, const char *tools_json);

/* replace_run: 完全替换 LLM 调用，由钩子自行产生最终输出 */
typedef err_t (*agent_hook_replace_run_fn)(struct message *msg,
	char *system_prompt, cJSON *messages, const char *tools_json,
	char **out_final_text);

/* on_finish: Turn 结束时调用，用于日志/统计/清理等无返回值操作 */
typedef void (*agent_hook_on_finish_fn)(struct message *msg, const char *response);

/* 扩展钩子虚表：注册时填充需要的钩子函数指针（未填的为 NULL） */
typedef struct {
	const char *name;			/* 扩展名称 */
	agent_hook_on_intent_fn   on_intent;	/* 意图阶段钩子 */
	agent_hook_on_prepare_fn  on_prepare;	/* 准备阶段钩子 */
	agent_hook_before_run_fn  before_run;	/* 运行前钩子 */
	agent_hook_replace_run_fn replace_run;	/* 替换运行钩子（完全接管 LLM 调用） */
	agent_hook_on_finish_fn   on_finish;	/* 结束钩子 */
	bool enabled;				/* 是否启用此扩展 */
} agent_extension_hooks_t;

#define AGENT_MAX_EXTENSIONS 16		/* 最大注册扩展数 */

/* 初始化钩子系统（清空注册表） */
void agent_hooks_init(void);

/* 注册一个扩展钩子集 */
void agent_hooks_register(agent_extension_hooks_t *hooks);

/* 触发各阶段钩子链 — 按注册顺序遍历，首个非 0 返回值即中断 */

err_t agent_hooks_trigger_intent(struct message *msg);
err_t agent_hooks_trigger_prepare(struct message *msg,
	char *system_prompt, size_t system_prompt_size, cJSON *messages);
err_t agent_hooks_trigger_before_run(struct message *msg,
	const char **model_override, const char *tools_json);
err_t agent_hooks_trigger_replace_run(struct message *msg,
	char *system_prompt, cJSON *messages, const char *tools_json,
	char **out_final_text);
void agent_hooks_trigger_finish(struct message *msg, const char *response);
