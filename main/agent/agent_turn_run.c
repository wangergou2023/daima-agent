#include "agent/agent_turn_run.h"
#include "agent/agent_turn_exec_helpers.h"
#include "agent/agent_cancel.h"
#include "agent/category_router.h"
#include "agent/session_recovery.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "llm/llm_proxy.h"
#include "llm/model_fallback.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"

static const char *TAG = "agent_run";

#define TOOL_OUTPUT_SIZE  (8 * 1024)

static bool mark_cancelled_if_needed(const daima_msg_t *msg,
                                     uint64_t cancel_token,
                                     bool *out_cancelled,
                                     const char *stage)
{
    if (!agent_cancel_is_cancelled(msg->chat_id, cancel_token)) {
        return false;
    }
    *out_cancelled = true;
    DAIMA_LOGI(TAG, "Agent turn cancelled %s: chat=%s", stage, msg->chat_id);
    return true;
}

static daima_err_t cancellable_llm_chat_tools(const daima_msg_t *msg,
                                               uint64_t cancel_token,
                                               const char *system_prompt,
                                               cJSON *messages,
                                               const char *tools_json,
                                               const char *model_override,
                                               llm_response_t *resp)
{
    agent_cancel_enter_current_turn(msg->chat_id, cancel_token);
    daima_err_t err = llm_chat_tools_with_model(system_prompt, messages, tools_json, model_override, resp);
    agent_cancel_leave_current_turn();
    return err;
}

static daima_err_t cancellable_model_fallback_chat_tools(const daima_msg_t *msg,
                                                         uint64_t cancel_token,
                                                         const char *system_prompt,
                                                         cJSON *messages,
                                                         const char *tools_json,
                                                         const char *model_override,
                                                         llm_response_t *resp)
{
    agent_cancel_enter_current_turn(msg->chat_id, cancel_token);
    char previous_model[64];
    snprintf(previous_model, sizeof(previous_model), "%s", llm_get_model_name());
    if (model_override && model_override[0]) {
        llm_set_model(model_override);
    }
    daima_err_t err = model_fallback_chat_with_fallback(system_prompt, messages, tools_json, resp);
    llm_set_model(previous_model);
    agent_cancel_leave_current_turn();
    return err;
}

static cJSON *cancellable_build_tool_results(const daima_msg_t *msg,
                                             uint64_t cancel_token,
                                             const llm_response_t *resp,
                                             char *tool_output,
                                             size_t tool_output_size,
                                             turn_exec_stats_t *stats)
{
    agent_cancel_enter_current_turn(msg->chat_id, cancel_token);
    cJSON *tool_results = agent_turn_build_tool_results(resp, msg, tool_output, tool_output_size, stats);
    agent_cancel_leave_current_turn();
    return tool_results;
}

daima_err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const daima_msg_t *msg,
    uint64_t cancel_token,
    char **out_final_text,
    char **out_reasoning_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted,
    bool *out_cancelled)
{
    if (!system_prompt || !messages || !msg || !out_final_text || !out_reasoning_text || !out_iteration || !out_tool_budget_exhausted || !out_cancelled) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *out_final_text = NULL;
    *out_reasoning_text = NULL;
    *out_iteration = 0;
    *out_tool_budget_exhausted = false;
    *out_cancelled = false;

    char *tool_output = daima_calloc(1, TOOL_OUTPUT_SIZE);
    if (!tool_output) {
        return DAIMA_ERR_NO_MEM;
    }

    daima_err_t err = DAIMA_OK;
    int iteration = 0;
    char *final_text = NULL;
    char *final_reasoning_text = NULL;
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    while (iteration < DAIMA_AGENT_MAX_TOOL_ITER) {
        if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "before LLM call")) {
            break;
        }

        const char *model_override = NULL;
#ifdef DAIMA_CATEGORY_ROUTING_ENABLED
        category_router_cfg_t cfg = category_router_load_and_get_cfg();
        if (cfg.enabled) {
            const daima_category_profile_t *profile = category_router_resolve(msg->intent);
            if (profile) {
                model_override = profile->model;
                DAIMA_LOGI(TAG, "Category routing: intent=%s -> model=%s",
                           daima_intent_name(msg->intent), profile->model);
            }
        }
#endif

        llm_response_t resp;
        memset(&resp, 0, sizeof(resp));
#ifdef DAIMA_MODEL_FALLBACK_ENABLED
        err = cancellable_model_fallback_chat_tools(msg, cancel_token, system_prompt, messages, tools_json, model_override, &resp);
#else
        err = cancellable_llm_chat_tools(msg, cancel_token, system_prompt, messages, tools_json, model_override, &resp);
#endif

        if (err != DAIMA_OK) {
            if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "during LLM call")) {
                err = DAIMA_OK;
                break;
            }
#ifdef DAIMA_SESSION_RECOVERY_ENABLED
            session_recovery_save_crash(msg->chat_id, msg->content, daima_err_to_name(err));
#endif
            DAIMA_LOGE(TAG, "LLM call failed: %s", daima_err_to_name(err));
            break;
        }

        if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "after LLM call")) {
            llm_response_free(&resp);
            break;
        }

        if (!resp.tool_use) {
            if (resp.text && resp.text_len > 0) {
                final_text = strdup(resp.text);
            }
            if (resp.reasoning_content && resp.reasoning_content_len > 0) {
                final_reasoning_text = strdup(resp.reasoning_content);
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

        cJSON *tool_results = cancellable_build_tool_results(
            msg, cancel_token, &resp, tool_output, TOOL_OUTPUT_SIZE, &stats);
        cJSON *result_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(result_msg, "role", "user");
        cJSON_AddItemToObject(result_msg, "content", tool_results);
        cJSON_AddItemToArray(messages, result_msg);

        llm_response_free(&resp);
        iteration++;

        if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "after tool execution")) {
            break;
        }

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

    if (!*out_cancelled && !final_text && iteration >= DAIMA_AGENT_MAX_TOOL_ITER) {
        *out_tool_budget_exhausted = true;
        DAIMA_LOGW(TAG, "Tool iteration budget exhausted for chat %s, forcing final response",
                 msg->chat_id);
        final_text = agent_turn_generate_forced_final_response(
            system_prompt,
            messages,
            "工具调用轮次已达上限。");
        err = DAIMA_OK;
    }

    if (!*out_cancelled) {
        agent_turn_maybe_run_auto_verification(&stats, &final_text);
    }

    free(tool_output);
    *out_final_text = final_text;
    *out_reasoning_text = final_reasoning_text;
    *out_iteration = iteration;
    return err;
}
