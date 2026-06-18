/* 计划评审系统接口。
 * PLANNER agent 生成执行计划并存储于 plan 结构体中，
 * 随后注入到 system prompt 中以指导 EXECUTOR agent 按步骤执行。
 * Plan 不可包含 TODO/TBD 占位符 — plan_review_generate 会检测并拒绝。 */

#pragma once

#include "intent.h"
#include "err.h"

#include <stdbool.h>
#include <stddef.h>

/* 执行计划：PLANNER 生成并经评审后的分步执行方案 */
struct plan {
	char plan_text[4096];	/* 计划正文（分步指令文本） */
	bool has_plan;		/* 是否已生成有效计划 */
	bool reviewed;		/* 是否已通过评审（无 TODO/TBD 占位符） */
};

/**
 * 根据用户消息和意图生成执行计划（调用 PLANNER LLM）。
 * 生成的计划会存入 out_plan 中，并自动检测占位符。
 * @param intent        用户消息意图
 * @param user_message  用户原始消息
 * @param system_prompt 当前系统提示词
 * @param out_plan      输出：填充的 plan 结构体
 * @return 成功返回 0
 */
err_t plan_review_generate(enum intent intent,
				  const char *user_message,
				  const char *system_prompt,
				  struct plan *out_plan);

/**
 * 将评审后的计划注入到 system prompt 末尾。
 * 追加格式化的分步指令，指导 EXECUTOR 按计划执行。
 * @param plan               已评审的计划
 * @param system_prompt      系统提示词缓冲区（会追加内容）
 * @param system_prompt_size 缓冲区大小
 * @return 成功返回 0
 */
err_t plan_review_inject_to_prompt(const struct plan *plan,
					   char *system_prompt,
					   size_t system_prompt_size);
