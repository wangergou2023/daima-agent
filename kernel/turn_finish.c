/* Turn 收尾入口：串接 reply 处理和后处理副作用。 */

#include "turn_finish.h"

#include "turn_post.h"
#include "turn_reply.h"

void agent_turn_finish(
	struct message *msg,
	char **io_final_text,
	char **io_reasoning_text,
	err_t turn_err,
	int iteration,
	bool tool_budget_exhausted,
	bool cancelled)
{
	agent_turn_handle_reply(msg, io_final_text, io_reasoning_text, turn_err,
				iteration, tool_budget_exhausted, cancelled);
	agent_turn_run_post_actions(msg, turn_err, cancelled);
}
