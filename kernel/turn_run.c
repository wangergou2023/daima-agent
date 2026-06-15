#include "turn_run.h"
#include "turn_exec.h"
#include "cancel.h"
#include "recovery.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "drivers/llm/llm_proxy.h"
#include "drivers/llm/model_fallback.h"
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "linux/slab.h"
#define TOOL_OUTPUT_SIZE  (8 * 1024)

static bool mark_cancelled_if_needed(const struct message *msg,
                                     uint64_t cancel_token,
                                     bool *out_cancelled,
                                     const char *stage)
{
    if (!agent_cancel_is_cancelled(msg->chat_id, cancel_token)) {
        return false;
    }
    *out_cancelled = true;
    pr_info("Agent turn cancelled %s: chat=%s", stage, msg->chat_id);
    return true;
}

static err_t cancellable_llm_chat_tools(const struct message *msg,
                                               uint64_t cancel_token,
                                               const char *system_prompt,
                                               cJSON *messages,
                                               const char *tools_json,
                                               const char *model_override,
                                               llm_response_t *resp)
{
    agent_cancel_enter_current_turn(msg->chat_id, cancel_token);
    err_t err = llm_chat_tools_with_model(system_prompt, messages, tools_json, model_override, resp);
    agent_cancel_leave_current_turn();
    return err;
}

static err_t cancellable_model_fallback_chat_tools(const struct message *msg,
                                                         uint64_t cancel_token,
                                                         const char *system_prompt,
                                                         cJSON *messages,
                                                         const char *tools_json,
                                                         const char *model_override,
                                                         llm_response_t *resp)
{
    agent_cancel_enter_current_turn(msg->chat_id, cancel_token);
    char previous_model[64];
    strscpy(previous_model, llm_get_model_name(), sizeof(previous_model));
    if (model_override && model_override[0]) {
        llm_set_model(model_override);
    }
    err_t err = model_fallback_chat_with_fallback(system_prompt, messages, tools_json, resp);
    llm_set_model(previous_model);
    agent_cancel_leave_current_turn();
    return err;
}

static cJSON *cancellable_build_tool_results(const struct message *msg,
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

err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const struct message *msg,
    const char *model_override,
    uint64_t cancel_token,
    char **out_final_text,
    char **out_reasoning_text,
    int *out_iteration,
    bool *out_tool_budget_exhausted,
    bool *out_cancelled)
{
    if (unlikely(!system_prompt || !messages || !msg || !out_final_text || !out_reasoning_text || !out_iteration || !out_tool_budget_exhausted || !out_cancelled)) {
        return ERR_INVALID_ARG;
    }

    *out_final_text = NULL;
    *out_reasoning_text = NULL;
    *out_iteration = 0;
    *out_tool_budget_exhausted = false;
    *out_cancelled = false;

    char *tool_output = platform_calloc(1, TOOL_OUTPUT_SIZE);
    if (unlikely(!tool_output)) {
        return ERR_NO_MEM;
    }

    err_t err = 0;
    int iteration = 0;
    char *final_text = NULL;
    char *final_reasoning_text = NULL;
    turn_exec_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    while (iteration < AGENT_MAX_TOOL_ITER) {
        if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "before LLM call")) {
            break;
        }

        llm_response_t resp;
        memset(&resp, 0, sizeof(resp));
        if (IS_ENABLED(CONFIG_MODEL_FALLBACK_ENABLED)) {
            err = cancellable_model_fallback_chat_tools(msg, cancel_token, system_prompt, messages, tools_json, model_override, &resp);
        } else {
            err = cancellable_llm_chat_tools(msg, cancel_token, system_prompt, messages, tools_json, model_override, &resp);
        }

        if (unlikely(err != 0)) {
            if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "during LLM call")) {
                err = 0;
                break;
            }
            if (IS_ENABLED(CONFIG_SESSION_RECOVERY_ENABLED)) {
                session_recovery_save_crash(msg->chat_id, msg->content, err_name(err));
            }
            pr_err("LLM call failed: %s", err_name(err));
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
            err = 0;
            break;
        }

        pr_info("Tool use iteration %d: %d calls", iteration + 1, resp.call_count);

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
            pr_warn("Unrecoverable tool protocol error for chat %s: %s", msg->chat_id, stats.tool_protocol_error_reason);
            final_text = agent_turn_generate_forced_final_response(
                system_prompt,
                messages,
                "工具调用协议出现不可恢复错误。");
            err = 0;
            break;
        }
    }

    if (!*out_cancelled && !final_text && iteration >= AGENT_MAX_TOOL_ITER) {
        *out_tool_budget_exhausted = true;
        pr_warn("Tool iteration budget exhausted for chat %s, forcing final response", msg->chat_id);
        final_text = agent_turn_generate_forced_final_response(
            system_prompt,
            messages,
            "工具调用轮次已达上限。");
        err = 0;
    }

    if (!*out_cancelled) {
        agent_turn_maybe_run_auto_verification(&stats, &final_text);
    }

    kfree(tool_output);
    *out_final_text = final_text;
    *out_reasoning_text = final_reasoning_text;
    *out_iteration = iteration;
    return err;
}
