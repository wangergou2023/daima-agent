/* 单回合主链入口：前置判断、决策生成、prepare/run/finish 串接。 */
#include "turn_entry.h"

#include "turn_decision.h"
#include "turn_finish.h"
#include "turn_gate.h"
#include "turn_io.h"
#include "turn_pipeline.h"
#include "turn_prompt.h"
#include "turn_prepare.h"
#include "drivers/tool/tool_bus_view.h"

static void agent_turn_finish_prepare_error(struct message *msg, err_t err)
{
	char *final_text = NULL;
	char *reasoning_text = NULL;

	agent_turn_finish(msg, &final_text, &reasoning_text, err, 0, false, false);
}

static void agent_turn_run_from_prepared(struct message *msg,
					 agent_turn_io_t *io,
					 const agent_turn_decision_t *decision)
{
	const char *tools_json = tool_bus_tools_json_for_channel(msg->channel);
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

	agent_turn_io_t io = {0};
	if (!agent_turn_io_init(&io)) {
		return;
	}

	agent_turn_decision_t decision;
	agent_turn_decision_reset(&decision);
	agent_turn_decide(msg, &decision);

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
