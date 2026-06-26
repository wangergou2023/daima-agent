/* Turn reply 处理：统一 cancelled / success / error 回复路径。 */

#include "turn_reply.h"

#include <stdlib.h>

#include "ralph.h"
#include "turn_persist.h"
#include "linux/kernel.h"
#include "linux/slab.h"

void agent_turn_handle_reply(struct message *msg,
			     char **io_final_text,
			     char **io_reasoning_text,
			     err_t turn_err,
			     int iteration,
			     bool tool_budget_exhausted,
			     bool cancelled)
{
	char *final_text = io_final_text ? *io_final_text : NULL;
	char *reasoning_text = io_reasoning_text ? *io_reasoning_text : NULL;

	if (cancelled) {
		kfree(final_text);
		kfree(reasoning_text);
		final_text = NULL;
		reasoning_text = NULL;
		if (io_final_text) {
			*io_final_text = NULL;
		}
		if (io_reasoning_text) {
			*io_reasoning_text = NULL;
		}
		pr_info("Skip final response for cancelled turn %s:%s",
			msg ? msg->channel : "-", msg ? msg->chat_id : "-");
		return;
	}

	if (final_text && final_text[0]) {
		ralph_loop_append_warning_if_needed(msg->chat_id, iteration, &final_text);
		agent_turn_save_session(msg, final_text, reasoning_text, iteration);
		agent_turn_queue_outbound_text(msg, final_text, reasoning_text, true);
		final_text = NULL;
		kfree(reasoning_text);
		reasoning_text = NULL;
	} else {
		kfree(final_text);
		kfree(reasoning_text);
		reasoning_text = NULL;
		final_text = agent_turn_build_error_reply(tool_budget_exhausted);
		if (final_text) {
			agent_turn_queue_outbound_text(msg, final_text, NULL, true);
			final_text = NULL;
		}
	}

	if (io_final_text) {
		*io_final_text = final_text;
	}
	if (io_reasoning_text) {
		*io_reasoning_text = reasoning_text;
	}

	if (turn_err != 0) {
		pr_err("Agent turn failed: %s", err_name(turn_err));
	}
}
