/* Turn 运行阶段：LLM 工具调用主循环。
 * 在 AGENT_MAX_TOOL_ITER 轮次内反复调用 LLM → 工具执行 → 模型回退，
 * 直到 LLM 返回纯文本（无工具调用）、取消、或用尽预算。 */

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

static bool reasoning_has_dsml_tool_markup(const char *text)
{
    return text &&
           (strstr(text, "<｜｜DSML｜｜tool_calls>") ||
            strstr(text, "<｜｜DSML｜｜invoke ") ||
            strstr(text, "<｜｜DSML｜｜parameter "));
}

static void strip_dsml_tool_markup_inplace(char *text)
{
    if (!reasoning_has_dsml_tool_markup(text)) {
        return;
    }

    char *tool_calls = strstr(text, "<｜｜DSML｜｜tool_calls>");
    if (!tool_calls) {
        tool_calls = strstr(text, "<｜｜DSML｜｜invoke ");
    }
    if (!tool_calls) {
        tool_calls = strstr(text, "<｜｜DSML｜｜parameter ");
    }
    if (!tool_calls) {
        return;
    }

    while (tool_calls > text && (tool_calls[-1] == '\n' || tool_calls[-1] == '\r')) {
        tool_calls--;
    }
    *tool_calls = '\0';

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

static void sanitize_dsml_reply_text(char **io_text)
{
    if (!io_text || !*io_text) {
        return;
    }
    if (!reasoning_has_dsml_tool_markup(*io_text)) {
        return;
    }

    strip_dsml_tool_markup_inplace(*io_text);
    if (!(*io_text)[0]) {
        kfree(*io_text);
        *io_text = NULL;
    }
}

/** 检查取消令牌并标记取消状态。返回 true 表示已被取消。
 *  @param stage  描述当前阶段的字符串，用于日志 */
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

/** 可取消版 LLM 调用：进入当前轮取消上下文后调用 llm_chat_tools_with_model。 */
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

/** 可取消版模型回退调用：临时设置覆盖模型，调用后恢复原模型。 */
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

/** 可取消版工具结果构建：进入取消上下文后执行 agent_turn_build_tool_results。 */
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

/** Turn 主执行循环入口。
 *  运行 LLM→工具→LLM 循环，最多 AGENT_MAX_TOOL_ITER 轮。
 *  每轮支持取消检查、模型回退、非恢复性协议错误中断。
 *  用尽轮次或工具协议崩溃时自动生成强制最终回复。
 *  @param out_final_text          输出：最终回复文本（由调用方释放）
 *  @param out_reasoning_text      输出：最终推理文本
 *  @param out_iteration           输出：实际消耗轮次
 *  @param out_tool_budget_exhausted 输出：是否因轮次用尽而中止
 *  @param out_cancelled           输出：是否被取消令牌中断
 *  @return                        0 成功，ERR_INVALID_ARG / ERR_NO_MEM 失败 */
err_t agent_turn_run(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const struct message *msg,
    const char *model_override,
    int max_tool_iterations,
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

    if (max_tool_iterations <= 0) {
        max_tool_iterations = AGENT_MAX_TOOL_ITER;
    }

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

    while (iteration < max_tool_iterations) {
        /* 每次 LLM 调用前检查取消 */
        if (mark_cancelled_if_needed(msg, cancel_token, out_cancelled, "before LLM call")) {
            break;
        }

        llm_response_t resp;
        memset(&resp, 0, sizeof(resp));
        /* 根据配置选择直连 LLM 或走模型回退路径 */
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
            /* LLM 调用失败时保存崩溃快照供下次恢复 */
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

        /* 无工具调用 → LLM 给出最终文本回答，结束循环 */
        if (!resp.tool_use) {
            if (resp.text && resp.text_len > 0) {
                final_text = strdup(resp.text);
                sanitize_dsml_reply_text(&final_text);
            }
            if (resp.reasoning_content && resp.reasoning_content_len > 0) {
                final_reasoning_text = strdup(resp.reasoning_content);
                sanitize_dsml_reply_text(&final_reasoning_text);
            }
            if (!final_text && final_reasoning_text) {
                final_text = strdup(final_reasoning_text);
            }
            llm_response_free(&resp);
            err = 0;
            break;
        }

        pr_info("Tool use iteration %d: %d calls", iteration + 1, resp.call_count);

        /* 将 assistant 消息（含工具调用）追加进历史 */
        cJSON *asst_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(asst_msg, "role", "assistant");
        cJSON_AddItemToObject(asst_msg, "content", agent_turn_build_assistant_content(&resp));
        cJSON_AddItemToArray(messages, asst_msg);

        /* 将工具执行结果以 user 角色追加进历史，LLM 可据此决定下一步 */
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

        /* 检测不可恢复的工具协议错误（如模型幻觉出不存在的工具名） */
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

    /* 主循环结束后，若工具预算耗尽且无输出，强制生成最终回复 */
    if (!*out_cancelled && !final_text && iteration >= max_tool_iterations) {
        *out_tool_budget_exhausted = true;
        pr_warn("Tool iteration budget exhausted for chat %s, forcing final response", msg->chat_id);
        final_text = agent_turn_generate_forced_final_response(
            system_prompt,
            messages,
            "工具调用轮次已达上限。");
        err = 0;
    }

    /* 正常结束后执行自动构建验证（若代码有修改） */
    if (!*out_cancelled) {
        agent_turn_maybe_run_auto_verification(&stats, &final_text);
    }

    kfree(tool_output);
    *out_final_text = final_text;
    *out_reasoning_text = final_reasoning_text;
    *out_iteration = iteration;
    return err;
}
