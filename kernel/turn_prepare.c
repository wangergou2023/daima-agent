/* Turn 准备阶段入口：串接 prompt 构建与 messages 组装。 */

#include "turn_prepare.h"

#include "turn_message_build.h"
#include "turn_prompt_build.h"

err_t agent_turn_prepare(
	const struct message *msg,
	const struct plan *plan,
	char *system_prompt,
	size_t system_prompt_size,
	char *history_json,
	size_t history_json_size,
	cJSON **out_messages)
{
	if (!msg || !system_prompt || system_prompt_size == 0 ||
	    !history_json || history_json_size == 0 || !out_messages) {
		return ERR_INVALID_ARG;
	}

	err_t err = agent_turn_build_prompt(msg, plan, system_prompt, system_prompt_size);
	if (err != 0) {
		return err;
	}

	return agent_turn_build_messages(msg, history_json, history_json_size, out_messages);
}
