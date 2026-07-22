/* 已准备 turn 的统一执行与收尾流程。 */

#include "turn_pipeline.h"

#include "cancel.h"
#include "cjson.h"
#include "delegate/delegate_turn_directive.h"
#include "drivers/tool/tool_delegate.h"
#include "drivers/tool/tool_runtime.h"
#include "turn_finish.h"
#include "turn_interview.h"
#include "turn_run.h"
#include "turn_common.h"
#include "intent.h"
#include "linux/printk.h"

static char *turn_pipeline_extract_coordinator_id(const char *tool_output)
{
	cJSON *root;
	const char *coordinator_id;
	char *result;

	if (!tool_output || !tool_output[0]) {
		return NULL;
	}

	root = cJSON_Parse(tool_output);
	if (!root) {
		return NULL;
	}

	coordinator_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "coordinator_id"));
	result = (coordinator_id && coordinator_id[0]) ? strdup(coordinator_id) : NULL;
	cJSON_Delete(root);
	return result;
}

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
				    bool *out_cancelled,
				    turn_exec_stats_t *out_stats)
{
	char directive_buf[4096];
	pr_info("TurnPipeline begin: chat=%s channel=%s source=%s intent=%s content=%.160s",
		msg ? msg->chat_id : "",
		msg ? msg->channel : "",
		msg ? agent_msg_source_or_default(msg) : "",
		msg ? intent_name(msg->intent) : "UNKNOWN",
		(msg && msg->content) ? msg->content : "");
	agent_turn_interview_result_t interview = {0};
	err_t interview_err = agent_turn_try_interview(msg, out_final_text, &interview);
	pr_info("TurnPipeline interview result: chat=%s err=%s handled=%d continue_turn=%d final_text=%s",
		msg ? msg->chat_id : "",
		err_name(interview_err),
		interview.handled ? 1 : 0,
		interview.continue_turn ? 1 : 0,
		(out_final_text && *out_final_text) ? "set" : "null");
	if (interview_err == 0 && interview.handled && !interview.continue_turn) {
		return 0;
	}
	if (interview_err != 0 && interview.handled) {
		return interview_err;
	}

	directive_buf[0] = '\0';
	if (msg &&
	    msg->chat_id[0] &&
	    delegate_turn_directive_load_copy(msg->chat_id, directive_buf, sizeof(directive_buf))) {
		llm_tool_call_t call = {0};
		tool_runtime_result_t rt = {0};
		char tool_output[8192];

		strscpy(call.id, "turn_pipeline_delegate_directive", sizeof(call.id));
		strscpy(call.name, "delegate_task", sizeof(call.name));
		call.input = directive_buf;
		call.input_len = strlen(directive_buf);
		tool_output[0] = '\0';

		pr_info("TurnPipeline shortcut: chat=%s executing stored delegate directive directly",
			msg->chat_id);
		err_t tool_err = tool_runtime_execute_call(&call, msg, tool_output, sizeof(tool_output), &rt);
		if (tool_err != 0) {
			if (out_final_text) {
				*out_final_text = strdup(tool_output[0] ? tool_output : "delegate directive execution failed");
			}
			return tool_err;
		}
		if (out_final_text) {
			char coordinator_reply[256];
			char *coordinator_id = turn_pipeline_extract_coordinator_id(tool_output);
			if (coordinator_id && coordinator_id[0]) {
				snprintf(coordinator_reply,
					 sizeof(coordinator_reply),
					 "已启动后台子任务，coordinator_id=%s。后续进度和完成结果将通过实时事件返回。",
					 coordinator_id);
				*out_final_text = strdup(coordinator_reply);
			} else {
				*out_final_text = strdup(tool_output);
			}
			free(coordinator_id);
		}
		if (out_iteration) {
			*out_iteration = 1;
		}
		return 0;
	}

	uint64_t cancel_token = agent_cancel_begin_turn(cancel_chat_id);
	return agent_turn_run(system_prompt, messages, tools_json, msg, model_override,
			      false,
			      0,
			      cancel_token, out_final_text, out_reasoning_text,
			      out_iteration, out_tool_budget_exhausted,
			      out_cancelled, out_stats);
}

void agent_run_prepared_turn(struct message *msg,
			     char *system_prompt,
			     cJSON *messages,
			     const char *tools_json,
			     const char *model_override,
			     const char *cancel_chat_id,
			     int iteration_offset,
			     const agent_turn_decision_t *decision)
{
	char *final_text = NULL;
	char *reasoning_text = NULL;
	int iteration = 0;
	bool tool_budget_exhausted = false;
	bool cancelled = false;
	turn_exec_stats_t stats;
	memset(&stats, 0, sizeof(stats));

	err_t err = run_prepared_turn_once(msg, system_prompt, messages, tools_json,
					   model_override, cancel_chat_id,
					   &final_text, &reasoning_text, &iteration,
					   &tool_budget_exhausted, &cancelled, &stats);

	agent_finalize_turn(msg, &final_text, &reasoning_text, err, iteration,
			    tool_budget_exhausted, cancelled, iteration_offset,
			    &stats, decision);
}

void agent_finalize_turn(struct message *msg,
			 char **io_final_text,
			 char **io_reasoning_text,
			 err_t turn_err,
			 int iteration,
			 bool tool_budget_exhausted,
			 bool cancelled,
			 int iteration_offset,
			 const turn_exec_stats_t *stats,
			 const agent_turn_decision_t *decision)
{
	agent_turn_finish(msg, io_final_text, io_reasoning_text, turn_err,
			  iteration + iteration_offset, tool_budget_exhausted, cancelled,
			  stats, decision);
}
