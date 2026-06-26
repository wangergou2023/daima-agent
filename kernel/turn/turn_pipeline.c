/* 已准备 turn 的统一执行与收尾流程。 */

#include "turn_pipeline.h"

#include "cancel.h"
#include "turn_finish.h"
#include "turn_interview.h"
#include "turn_run.h"

static err_t run_prepared_turn_once(struct message *msg,
				    char *system_prompt,
				    cJSON *messages,
				    const char *tools_json,
				    const char *model_override,
				    const char *cancel_chat_id,
				    char **out_final_text,
				    char **out_reasoning_text,
				    int *out_iteration,
				    bool *out_tool_budget_exhausted,
				    bool *out_cancelled)
{
	err_t interview_err = agent_turn_try_interview(msg, out_final_text);
	if (interview_err == 0) {
		return 0;
	}

	uint64_t cancel_token = agent_cancel_begin_turn(cancel_chat_id);
	return agent_turn_run(system_prompt, messages, tools_json, msg, model_override,
			      0,
			      cancel_token, out_final_text, out_reasoning_text,
			      out_iteration, out_tool_budget_exhausted,
			      out_cancelled);
}

void agent_run_prepared_turn(struct message *msg,
			     char *system_prompt,
			     cJSON *messages,
			     const char *tools_json,
			     const char *model_override,
			     const char *cancel_chat_id,
			     int iteration_offset)
{
	char *final_text = NULL;
	char *reasoning_text = NULL;
	int iteration = 0;
	bool tool_budget_exhausted = false;
	bool cancelled = false;

	err_t err = run_prepared_turn_once(msg, system_prompt, messages, tools_json,
					   model_override, cancel_chat_id,
					   &final_text, &reasoning_text, &iteration,
					   &tool_budget_exhausted, &cancelled);

	agent_finalize_turn(msg, &final_text, &reasoning_text, err, iteration,
			    tool_budget_exhausted, cancelled, iteration_offset);
}

void agent_finalize_turn(struct message *msg,
			 char **io_final_text,
			 char **io_reasoning_text,
			 err_t turn_err,
			 int iteration,
			 bool tool_budget_exhausted,
			 bool cancelled,
			 int iteration_offset)
{
	agent_turn_finish(msg, io_final_text, io_reasoning_text, turn_err,
			  iteration + iteration_offset, tool_budget_exhausted, cancelled);
}
