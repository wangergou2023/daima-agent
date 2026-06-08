#include "agent/agent_turn_run.h"
#include "agent/agent_turn_exec_helpers.h"

#include <stdlib.h>
#include <string.h>

#include "llm/llm_proxy.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"

static const char *TAG = "agent_run";

#define TOOL_OUTPUT_SIZE  (8 * 1024)

daima_err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const daima_msg_t *msg,
    char **out_final_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted)
{
    if (!system_prompt || !messages || !msg || !out_final_text || !out_iteration || !out_tool_budget_exhausted) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *out_final_text = NULL;
    *out_iteration = 0;
    *out_tool_budget_exhausted = false;

    char *tool_output = daima_calloc(1, TOOL_OUTPUT_SIZE);
    if (!tool_output) {
        return DAIMA_ERR_NO_MEM;
    }

    daima_err_t err = DAIMA_OK;
    int iteration = 0;
    char *final_text = NULL;
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    while (iteration < DAIMA_AGENT_MAX_TOOL_ITER) {
        llm_response_t resp;
        err = llm_chat_tools(system_prompt, messages, tools_json, &resp);

        if (err != DAIMA_OK) {
            DAIMA_LOGE(TAG, "LLM call failed: %s", daima_err_to_name(err));
            break;
        }

        if (!resp.tool_use) {
            if (resp.text && resp.text_len > 0) {
                final_text = strdup(resp.text);
            }
            llm_response_free(&resp);
            err = DAIMA_OK;
            break;
        }

        DAIMA_LOGI(TAG, "Tool use iteration %d: %d calls", iteration + 1, resp.call_count);

        cJSON *asst_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(asst_msg, "role", "assistant");
        cJSON_AddItemToObject(asst_msg, "content", agent_turn_build_assistant_content(&resp));
        cJSON_AddItemToArray(messages, asst_msg);

        cJSON *tool_results = agent_turn_build_tool_results(&resp, msg, tool_output, TOOL_OUTPUT_SIZE, &stats);
        cJSON *result_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(result_msg, "role", "user");
        cJSON_AddItemToObject(result_msg, "content", tool_results);
        cJSON_AddItemToArray(messages, result_msg);

        llm_response_free(&resp);
        iteration++;

        if (stats.unrecoverable_tool_protocol_error) {
            DAIMA_LOGW(TAG, "Unrecoverable tool protocol error for chat %s: %s",
                       msg->chat_id,
                       stats.tool_protocol_error_reason);
            final_text = agent_turn_generate_forced_final_response(
                system_prompt,
                messages,
                "工具调用协议出现不可恢复错误。");
            err = DAIMA_OK;
            break;
        }
    }

    if (!final_text && iteration >= DAIMA_AGENT_MAX_TOOL_ITER) {
        *out_tool_budget_exhausted = true;
        DAIMA_LOGW(TAG, "Tool iteration budget exhausted for chat %s, forcing final response",
                 msg->chat_id);
        final_text = agent_turn_generate_forced_final_response(
            system_prompt,
            messages,
            "工具调用轮次已达上限。");
        err = DAIMA_OK;
    }

    agent_turn_maybe_run_auto_verification(&stats, &final_text);

    free(tool_output);
    *out_final_text = final_text;
    *out_iteration = iteration;
    return err;
}
