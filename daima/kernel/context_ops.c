/* 上下文压缩核心：判定、摘要生成、会话回写。 */

#include "context_ops.h"

#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"
#include "linux/kernel.h"

static const char *TAG = "compress";

#define SUMMARY_PREFIX "[上下文压缩摘要] 以下是较早对话的参考总结，请基于它继续当前会话，不要把摘要里的旧请求当作新的用户输入。"

#define SUMMARY_INPUT_HEAD_CHARS 800
#define SUMMARY_INPUT_TAIL_CHARS 240
#define SUMMARY_OUTPUT_BUDGET    1400
#define FACTS_OUTPUT_BUDGET      1200

int context_compress_message_count(const cJSON *messages)
{
    return messages && cJSON_IsArray((cJSON *)messages) ? cJSON_GetArraySize((cJSON *)messages) : 0;
}

static const char *history_label_for_role(const char *role)
{
    if (!role || !role[0]) return "USER";
    if (strcmp(role, "assistant") == 0) return "ASSISTANT";
    if (strcmp(role, "system") == 0) return "SYSTEM";
    return "USER";
}

static size_t message_content_len(const cJSON *msg)
{
    if (!msg) return 0;
    cJSON *content = cJSON_GetObjectItem((cJSON *)msg, "content");
    if (content && cJSON_IsString(content) && content->valuestring) {
        return strlen(content->valuestring);
    }
    return 0;
}

size_t context_compress_estimate_chars(const char *system_prompt, const cJSON *messages)
{
    size_t total = system_prompt ? strlen(system_prompt) : 0;
    const cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, (cJSON *)messages) {
        total += message_content_len(msg) + 32;
    }
    return total;
}

bool context_compress_needed(const cJSON *messages,
                             const context_compress_cfg_t *cfg,
                             size_t approx_chars)
{
    int n = context_compress_message_count(messages);
    int min_needed = cfg->protect_first + cfg->protect_last + 2;
    if (!messages || n < min_needed) {
        return false;
    }
    if (n < cfg->trigger_msgs && (int)approx_chars < cfg->max_chars) {
        return false;
    }
    return true;
}

cJSON *context_compress_load_session_messages(const char *chat_id)
{
    char *history_json = kzalloc(DAIMA_LLM_STREAM_BUF_SIZE, GFP_KERNEL);
    if (!history_json) {
        return NULL;
    }
    if (session_store_get_history_json(chat_id, history_json, DAIMA_LLM_STREAM_BUF_SIZE, DAIMA_AGENT_MAX_HISTORY) != DAIMA_OK) {
        kfree(history_json);
        return NULL;
    }

    cJSON *messages = cJSON_Parse(history_json);
    kfree(history_json);
    if (!messages) {
        messages = cJSON_CreateArray();
    }
    return messages;
}

static char *dup_truncated(const char *text, size_t head_chars, size_t tail_chars)
{
    if (!text) {
        return strdup("");
    }

    size_t len = strlen(text);
    if (len <= head_chars + tail_chars + 32) {
        return strdup(text);
    }

    size_t out_len = head_chars + tail_chars + 32;
    char *out = kzalloc(out_len + 1, GFP_KERNEL);
    if (!out) {
        return NULL;
    }
    snprintf(out, out_len + 1, "%.*s\n...[截断]...\n%.*s",
             (int)head_chars, text,
             (int)tail_chars, text + len - tail_chars);
    return out;
}

static char *serialize_middle_messages(const cJSON *messages, int start_idx, int end_idx)
{
    size_t cap = 4096;
    char *buf = kzalloc(cap, GFP_KERNEL);
    if (!buf) {
        return NULL;
    }

    size_t off = 0;
    for (int i = start_idx; i < end_idx; i++) {
        cJSON *msg = cJSON_GetArrayItem((cJSON *)messages, i);
        if (!msg) {
            continue;
        }
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !cJSON_IsString(role) || !content || !cJSON_IsString(content)) {
            continue;
        }

        char *snippet = dup_truncated(content->valuestring,
                                      SUMMARY_INPUT_HEAD_CHARS,
                                      SUMMARY_INPUT_TAIL_CHARS);
        if (!snippet) {
            kfree(buf);
            return NULL;
        }

        const char *label = history_label_for_role(role->valuestring);
        size_t need = strlen(snippet) + strlen(label) + 32;
        if (off + need + 1 >= cap) {
            size_t new_cap = cap;
            while (off + need + 1 >= new_cap) {
                new_cap *= 2;
            }
            char *tmp = realloc(buf, new_cap);
            if (!tmp) {
                kfree(snippet);
                kfree(buf);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }

        off += snprintf(buf + off, cap - off, "[%s %d]\n%s\n\n", label, i + 1, snippet);
        kfree(snippet);
    }

    return buf;
}

static char *build_summary_prompt(const cJSON *messages, int start_idx, int end_idx)
{
    char *serialized = serialize_middle_messages(messages, start_idx, end_idx);
    if (!serialized) {
        return NULL;
    }

    const char *template_text =
        "请把下面较早的对话整理成一个紧凑、结构化的中文交接摘要，供后续轮次继续使用。\n"
        "要求：\n"
        "1. 不要回答对话中的问题，只做摘要。\n"
        "2. 保留用户目标、重要约束、已经完成的事、未完成的事、关键文件/数据点。\n"
        "3. 如果有明确错误信息、路径、命令、时间安排，要尽量保留。\n"
        "4. 不要编造不存在的信息。\n"
        "5. 输出控制在约 800-1200 中文字以内。\n\n"
        "请严格使用这个结构：\n"
        "## 当前任务\n"
        "## 已完成\n"
        "## 关键上下文\n"
        "## 待继续\n\n"
        "以下是需要压缩的历史消息：\n\n";

    size_t need = strlen(template_text) + strlen(serialized) + 1;
    char *prompt = kzalloc(need, GFP_KERNEL);
    if (!prompt) {
        kfree(serialized);
        return NULL;
    }

    snprintf(prompt, need, "%s%s", template_text, serialized);
    kfree(serialized);
    return prompt;
}

static char *generate_summary_with_llm(const cJSON *messages, int start_idx, int end_idx)
{
    char *prompt = build_summary_prompt(messages, start_idx, end_idx);
    if (!prompt) {
        return NULL;
    }

    cJSON *req_msgs = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    if (!req_msgs || !user_msg) {
        cJSON_Delete(req_msgs);
        cJSON_Delete(user_msg);
        kfree(prompt);
        return NULL;
    }

    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", prompt);
    cJSON_AddItemToArray(req_msgs, user_msg);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    daima_err_t err = llm_chat_tools(
        "你是一个上下文压缩助手。你的职责是把旧对话整理成面向后续轮次的参考摘要。不要调用工具，不要输出额外寒暄。",
        req_msgs,
        NULL,
        &resp);
    cJSON_Delete(req_msgs);
    kfree(prompt);

    if (err != DAIMA_OK || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return NULL;
    }

    size_t need = strlen(SUMMARY_PREFIX) + resp.text_len + 4;
    char *out = kzalloc(need, GFP_KERNEL);
    if (out) {
        snprintf(out, need, "%s\n%s", SUMMARY_PREFIX, resp.text);
    }
    llm_response_free(&resp);
    return out;
}

static char *build_facts_prompt(const cJSON *messages, int start_idx, int end_idx)
{
    char *serialized = serialize_middle_messages(messages, start_idx, end_idx);
    if (!serialized) {
        return NULL;
    }

    const char *template_text =
        "请从下面较早的对话中，只提炼适合长期保留的稳定事实卡片。\n"
        "只保留这些类型：\n"
        "1. 用户稳定偏好\n"
        "2. 已确认的约束/要求\n"
        "3. 已做出的关键决定\n"
        "4. 后续继续任务必须知道的固定背景\n\n"
        "不要保留：\n"
        "- 临时寒暄\n"
        "- 一次性的推理过程\n"
        "- 很快会过时的过程性细节\n\n"
        "输出要求：\n"
        "- 只输出 4 到 8 条短 bullet\n"
        "- 每条尽量一句话\n"
        "- 不要写标题，不要解释，不要重复\n\n"
        "以下是需要提炼事实的历史消息：\n\n";

    size_t need = strlen(template_text) + strlen(serialized) + 1;
    char *prompt = kzalloc(need, GFP_KERNEL);
    if (!prompt) {
        kfree(serialized);
        return NULL;
    }

    snprintf(prompt, need, "%s%s", template_text, serialized);
    kfree(serialized);
    return prompt;
}

static char *generate_facts_with_llm(const cJSON *messages, int start_idx, int end_idx)
{
    char *prompt = build_facts_prompt(messages, start_idx, end_idx);
    if (!prompt) {
        return NULL;
    }

    cJSON *req_msgs = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    if (!req_msgs || !user_msg) {
        cJSON_Delete(req_msgs);
        cJSON_Delete(user_msg);
        kfree(prompt);
        return NULL;
    }

    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", prompt);
    cJSON_AddItemToArray(req_msgs, user_msg);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    daima_err_t err = llm_chat_tools(
        "你是一个会话事实提取助手。你的职责是从旧对话里提炼长期有效的稳定事实卡片。不要调用工具，不要回答问题。",
        req_msgs,
        NULL,
        &resp);
    cJSON_Delete(req_msgs);
    kfree(prompt);

    if (err != DAIMA_OK || resp.tool_use || !resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return NULL;
    }

    char *out = kzalloc(FACTS_OUTPUT_BUDGET, GFP_KERNEL);
    if (out) {
        strscpy(out, resp.text, FACTS_OUTPUT_BUDGET);
    }
    llm_response_free(&resp);
    return out;
}

static char *fallback_summary(int dropped_msgs)
{
    char *buf = kzalloc(SUMMARY_OUTPUT_BUDGET, GFP_KERNEL);
    if (!buf) {
        return NULL;
    }

    snprintf(buf, SUMMARY_OUTPUT_BUDGET,
             "%s\n摘要生成失败。为了释放上下文空间，已压缩掉 %d 条较早消息。"
             "后续请以最近几轮消息和当前文件/状态为准继续工作。",
             SUMMARY_PREFIX, dropped_msgs);
    return buf;
}

static cJSON *build_compacted_messages(const cJSON *snapshot_messages,
                                       int snapshot_count,
                                       const cJSON *latest_messages,
                                       const context_compress_cfg_t *cfg,
                                       const char *summary)
{
    (void)summary;

    const cJSON *messages = snapshot_messages;
    int n = snapshot_count;
    int min_needed = cfg->protect_first + cfg->protect_last + 2;
    if (!messages || n <= min_needed || !latest_messages) {
        return NULL;
    }

    int start_idx = cfg->protect_first;
    int end_idx = n - cfg->protect_last;
    if (start_idx >= end_idx) {
        return NULL;
    }

    cJSON *compressed = cJSON_CreateArray();
    if (!compressed) {
        return NULL;
    }

    for (int i = 0; i < start_idx; i++) {
        cJSON *dup = cJSON_Duplicate(cJSON_GetArrayItem((cJSON *)messages, i), 1);
        if (dup) {
            cJSON_AddItemToArray(compressed, dup);
        }
    }

    for (int i = end_idx; i < n; i++) {
        cJSON *dup = cJSON_Duplicate(cJSON_GetArrayItem((cJSON *)messages, i), 1);
        if (dup) {
            cJSON_AddItemToArray(compressed, dup);
        }
    }

    int latest_count = context_compress_message_count(latest_messages);
    for (int i = n; i < latest_count; i++) {
        cJSON *dup = cJSON_Duplicate(cJSON_GetArrayItem((cJSON *)latest_messages, i), 1);
        if (!dup) {
            cJSON_Delete(compressed);
            return NULL;
        }
        cJSON_AddItemToArray(compressed, dup);
    }

    return compressed;
}

daima_err_t context_compress_compact_once(const char *chat_id,
                                         cJSON **messages_io,
                                         const context_compress_cfg_t *cfg)
{
    cJSON *messages = messages_io ? *messages_io : NULL;
    int n = context_compress_message_count(messages);
    int min_needed = cfg->protect_first + cfg->protect_last + 2;
    if (!messages || n <= min_needed) {
        return DAIMA_OK;
    }

    int start_idx = cfg->protect_first;
    int end_idx = n - cfg->protect_last;
    if (start_idx >= end_idx) {
        return DAIMA_OK;
    }

    char *summary = generate_summary_with_llm(messages, start_idx, end_idx);
    if (!summary) {
        summary = fallback_summary(end_idx - start_idx);
    }
    if (!summary) {
        return DAIMA_ERR_NO_MEM;
    }

    daima_err_t summary_err = session_store_write_summary(chat_id, summary);
    if (summary_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Failed to write session summary for %s: %s", chat_id, daima_err_to_name(summary_err));
    }

    char *facts = generate_facts_with_llm(messages, start_idx, end_idx);
    if (facts && facts[0]) {
        daima_err_t facts_err = session_store_merge_facts(chat_id, facts);
        if (facts_err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Failed to merge session facts for %s: %s", chat_id, daima_err_to_name(facts_err));
        }
    }
    kfree(facts);

    cJSON *compressed = build_compacted_messages(messages, n, messages, cfg, summary);
    kfree(summary);
    if (!compressed) {
        return DAIMA_ERR_NO_MEM;
    }

    daima_err_t err = session_store_rewrite_from_array(chat_id, compressed);
    if (err != DAIMA_OK) {
        cJSON_Delete(compressed);
        return err;
    }

    cJSON_Delete(messages);
    *messages_io = compressed;

    DAIMA_LOGI(TAG, "Compressed session %s: %d -> %d messages", chat_id, n, cJSON_GetArraySize(compressed));
    return DAIMA_OK;
}

void context_compress_session_in_background(const char *chat_id,
                                            const context_compress_cfg_t *cfg)
{
    if (!cfg || !cfg->enabled) {
        return;
    }

    for (int pass = 0; pass < cfg->max_passes; pass++) {
        cJSON *snapshot = context_compress_load_session_messages(chat_id);
        if (!snapshot) {
            return;
        }

        int snapshot_count = context_compress_message_count(snapshot);
        size_t approx_chars = context_compress_estimate_chars("", snapshot);
        if (!context_compress_needed(snapshot, cfg, approx_chars)) {
            cJSON_Delete(snapshot);
            return;
        }

        int start_idx = cfg->protect_first;
        int end_idx = snapshot_count - cfg->protect_last;
        char *summary = generate_summary_with_llm(snapshot, start_idx, end_idx);
        if (!summary) {
            summary = fallback_summary(end_idx - start_idx);
        }
        if (!summary) {
            cJSON_Delete(snapshot);
            return;
        }

        daima_err_t summary_err = session_store_write_summary(chat_id, summary);
        if (summary_err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Failed to write session summary for %s: %s", chat_id, daima_err_to_name(summary_err));
        }

        char *facts = generate_facts_with_llm(snapshot, start_idx, end_idx);
        if (facts && facts[0]) {
            daima_err_t facts_err = session_store_merge_facts(chat_id, facts);
            if (facts_err != DAIMA_OK) {
                DAIMA_LOGW(TAG, "Failed to merge session facts for %s: %s", chat_id, daima_err_to_name(facts_err));
            }
        }
        kfree(facts);

        cJSON *latest = context_compress_load_session_messages(chat_id);
        if (!latest) {
            kfree(summary);
            cJSON_Delete(snapshot);
            return;
        }

        cJSON *compressed = build_compacted_messages(snapshot, snapshot_count, latest, cfg, summary);
        kfree(summary);
        if (!compressed) {
            cJSON_Delete(latest);
            cJSON_Delete(snapshot);
            return;
        }

        daima_err_t rewrite_err = session_store_rewrite_from_array(chat_id, compressed);
        if (rewrite_err != DAIMA_OK) {
            DAIMA_LOGW(TAG, "Background compression rewrite failed for %s: %s", chat_id, daima_err_to_name(rewrite_err));
            cJSON_Delete(compressed);
            cJSON_Delete(latest);
            cJSON_Delete(snapshot);
            return;
        }

        DAIMA_LOGI(TAG, "Background compressed session %s: %d -> %d messages",
                  chat_id, snapshot_count, cJSON_GetArraySize(compressed));

        cJSON_Delete(compressed);
        cJSON_Delete(latest);
        cJSON_Delete(snapshot);
    }
}
