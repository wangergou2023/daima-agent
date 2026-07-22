/* Turn 持久化：保存会话历史、排队出站消息、生成错误回复。
 * turn_finish 阶段调用，将本轮 assistant 回复和用户消息写入 session_store。 */

#include "turn_persist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "turn_common.h"
#include "context_compress.h"
#include "learning.h"
#include "runtime.h"
#include "bus.h"
#include "drivers/memory/session_store.h"
#include "transcript.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#include "cjson.h"
#include "linux/slab.h"
#include "turn_dispatch.h"

/** 判断是否应将助手回复保存到会话历史（非内部消息且有内容）。 */
static bool should_save_assistant_reply(const struct message *msg, const char *final_text)
{
    return msg && final_text && final_text[0] && !agent_msg_is_internal_control(msg);
}

/** 构建助手消息的 JSON 载荷（text + 可选 reasoning）。调用方负责释放。 */
static char *build_assistant_session_content_json(const char *text, const char *reasoning)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "text", text ? text : "");
    if (reasoning && reasoning[0]) {
        cJSON_AddStringToObject(root, "reasoning", reasoning);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/** 将回复文本放入出站总线队列，由通道路由异步分发。
 *  @param free_on_fail  入队失败时是否释放 text（传 true 时调用方不持有所有权） */
void agent_turn_queue_outbound_text(const struct message *msg, char *text, const char *reasoning, bool free_on_fail)
{
    if (!msg || !text) {
        kfree(text);
        return;
    }
    if (agent_msg_is_internal_control(msg)) {
        pr_info("Skip outbound response for internal control message");
        kfree(text);
        return;
    }

    struct message out = {0};
    strncpy(out.channel, msg->channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, msg->chat_id, sizeof(out.chat_id) - 1);
    strncpy(out.source, agent_msg_source_or_default(msg), sizeof(out.source) - 1);
    out.content = text;
    out.reasoning = reasoning && reasoning[0] ? strdup(reasoning) : NULL;

    pr_info("Queue final response to %s:%s (%d bytes)", out.channel, out.chat_id, (int)strlen(text));
    if (message_bus_push_outbound(&out) != 0) {
        pr_warn("Outbound queue full, drop response");
        if (free_on_fail) {
            kfree(text);
        }
        kfree(out.reasoning);
    }
}

/** 保存本轮会话：用户消息 + assistant 回复 → session_store，并触发压缩和复盘。 */
void agent_turn_save_session(const struct message *msg, const char *final_text, const char *reasoning, int iteration)
{
    if (!msg || !msg->chat_id[0] || !final_text || !final_text[0]) {
        return;
    }

    const char *inbound_role = agent_session_role_for_inbound_msg(msg);

    if (inbound_role) {
        dispatch_save_session_sourced(msg->chat_id, inbound_role,
                                       msg->content ? msg->content : "",
                                       agent_msg_source_or_default(msg));
    }

    if (should_save_assistant_reply(msg, final_text)) {
        char *payload = build_assistant_session_content_json(final_text, reasoning);
        if (payload) {
            dispatch_save_session(msg->chat_id, "assistant", payload);
            kfree(payload);
        }
    }

    pr_info("Session saved for chat %s (source=%s)", msg->chat_id, agent_msg_source_or_default(msg));
    context_compressor_schedule_if_needed(msg->chat_id);
    if (iteration >= 1 && runtime_config_get_learning_review_enabled()) {
        learning_review_schedule(msg->chat_id);
    }
}

/** 构建并写入结构化 TranscriptRecord。 */
static void agent_turn_write_transcript(const struct message *msg,
                                        const char *final_text,
                                        int iteration,
                                        int duration_ms,
                                        int model_calls,
                                        int tool_calls,
                                        int total_tokens,
                                        const char *routing_decision,
                                        const char *match_agent_id,
                                        float match_score,
                                        const char *executed_by)
{
    if (!msg || !msg->chat_id[0])
        return;

    transcript_record_t rec;
    memset(&rec, 0, sizeof(rec));

    /* record_id: txn-YYYYMMDD-chat_slug-seq */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(rec.record_id, sizeof(rec.record_id),
             "txn-%04d%02d%02d-%s-%d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             msg->chat_id, iteration);

    snprintf(rec.task_id, sizeof(rec.task_id), "%s-%d", msg->chat_id, iteration);
    strscpy(rec.chat_id, msg->chat_id, sizeof(rec.chat_id));

    if (msg->content)
        strscpy(rec.user_input, msg->content, sizeof(rec.user_input));

    /* 提取技能标签（从用户输入 + 助手输出中匹配关键词） */
    transcript_extract_skill_tags(msg->content ? msg->content : "",
                                  final_text,
                                  rec.skill_tags, sizeof(rec.skill_tags));

    /* execution_summary 截取 final_text 前列 */
    if (final_text) {
        strscpy(rec.execution_summary, final_text, sizeof(rec.execution_summary));
        strscpy(rec.output, final_text, sizeof(rec.output));
    }

    /* 结果状态 */
    strscpy(rec.result_status, final_text ? "success" : "failure",
            sizeof(rec.result_status));

    /* 谁执行的 */
    strscpy(rec.executed_by, executed_by ? executed_by : "boss",
            sizeof(rec.executed_by));
    if (match_agent_id && match_agent_id[0])
        strscpy(rec.agent_id, match_agent_id, sizeof(rec.agent_id));

    /* 指标 */
    rec.total_tokens = total_tokens;
    rec.model_calls = model_calls;
    rec.tool_calls = tool_calls;
    rec.duration_ms = duration_ms;

    /* 路由决策 */
    strscpy(rec.routing_decision, routing_decision ? routing_decision : "fallback",
            sizeof(rec.routing_decision));
    if (match_agent_id)
        strscpy(rec.match_agent_id, match_agent_id, sizeof(rec.match_agent_id));
    rec.match_score = match_score;

    rec.timestamp = now;

    transcript_append(&rec);
}

/** 保存会话 + 结构化Transcript。
 *  在原有 agent_turn_save_session() 基础上追加 Transcript 写入。 */
void agent_turn_save_session_with_transcript(
    const struct message *msg,
    const char *final_text,
    const char *reasoning,
    int iteration,
    int duration_ms,
    int model_calls,
    int tool_calls,
    int total_tokens,
    const char *routing_decision,
    const char *match_agent_id,
    float match_score,
    const char *executed_by)
{
    /* 先写原有会话历史 */
    agent_turn_save_session(msg, final_text, reasoning, iteration);

    /* 再写结构化 Transcript */
    agent_turn_write_transcript(msg, final_text, iteration,
                                duration_ms, model_calls, tool_calls,
                                total_tokens,
                                routing_decision, match_agent_id,
                                match_score, executed_by);
}

/** 生成错误回复文本（工具预算耗尽时的中文提示或通用英文报错）。 */
char *agent_turn_build_error_reply(bool tool_budget_exhausted)
{
    if (tool_budget_exhausted) {
        return strdup("我已经把可用工具轮次用完了，但还没来得及整理出最终结果。请让我减少探索步骤后再试一次，或把目标范围再收窄一点。");
    }
    return strdup("Sorry, I encountered an error.");
}
