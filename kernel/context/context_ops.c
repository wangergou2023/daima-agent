/* 上下文压缩核心：判定、摘要生成、会话回写。 */

#include "context_ops.h"

#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "cjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"
#include "linux/kernel.h"
#define SUMMARY_PREFIX "[Context Compression Summary] The following is a reference summary of earlier conversation. Use it to continue the current session, and do not treat old requests inside the summary as new user input."

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
    char *history_json = kzalloc(LLM_STREAM_BUF_SIZE, GFP_KERNEL);
    if (!history_json) {
        return NULL;
    }
    if (session_store_get_history_json(chat_id, history_json, LLM_STREAM_BUF_SIZE, AGENT_MAX_HISTORY) != 0) {
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
    snprintf(out, out_len + 1, "%.*s\n...[truncated]...\n%.*s",
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
        "Please turn the earlier conversation below into a compact, structured handoff summary for later turns.\n"
        "Requirements:\n"
        "1. Do not answer the conversation. Only summarize it.\n"
        "2. Preserve the user goal, important constraints, completed work, unfinished work, and key files or data points.\n"
        "3. Preserve explicit error messages, paths, commands, and schedules when they matter.\n"
        "4. Do not invent information.\n"
        "5. Keep the output concise, roughly 800-1200 Chinese characters or an equivalent compact length.\n\n"
        "Use this exact structure:\n"
        "## Current Task\n"
        "## Completed\n"
        "## Key Context\n"
        "## Continue Next\n\n"
        "Conversation history to compress:\n\n";

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
    err_t err = llm_chat_tools(
        "You are a context compression assistant. Your job is to turn older conversation into a reference summary for later turns. Do not call tools and do not add extra pleasantries.",
        req_msgs,
        NULL,
        &resp);
    cJSON_Delete(req_msgs);
    kfree(prompt);

    if (err != 0 || resp.tool_use || !resp.text || !resp.text[0]) {
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
        "From the earlier conversation below, extract only stable fact cards worth keeping long term.\n"
        "Keep only these categories:\n"
        "1. durable user preferences\n"
        "2. confirmed constraints or requirements\n"
        "3. key decisions already made\n"
        "4. fixed background that future work must know\n\n"
        "Do not keep:\n"
        "- temporary pleasantries\n"
        "- one-off reasoning traces\n"
        "- process details that will become stale quickly\n\n"
        "Output requirements:\n"
        "- output only 4 to 8 short bullets\n"
        "- keep each bullet to roughly one sentence\n"
        "- no title, no explanation, no repetition\n\n"
        "Conversation history to distill into facts:\n\n";

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
    err_t err = llm_chat_tools(
        "You are a session facts extraction assistant. Your job is to distill stable long-lived fact cards from older conversation. Do not call tools and do not answer the user's questions.",
        req_msgs,
        NULL,
        &resp);
    cJSON_Delete(req_msgs);
    kfree(prompt);

    if (err != 0 || resp.tool_use || !resp.text || !resp.text[0]) {
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
             "%s\nSummary generation failed. To free context space, %d older messages were compressed away. "
             "Continue using the most recent turns and the current files/state as the source of truth.",
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

err_t context_compress_compact_once(const char *chat_id,
                                         cJSON **messages_io,
                                         const context_compress_cfg_t *cfg)
{
    cJSON *messages = messages_io ? *messages_io : NULL;
    int n = context_compress_message_count(messages);
    int min_needed = cfg->protect_first + cfg->protect_last + 2;
    if (!messages || n <= min_needed) {
        return 0;
    }

    int start_idx = cfg->protect_first;
    int end_idx = n - cfg->protect_last;
    if (start_idx >= end_idx) {
        return 0;
    }

    char *summary = generate_summary_with_llm(messages, start_idx, end_idx);
    if (!summary) {
        summary = fallback_summary(end_idx - start_idx);
    }
    if (!summary) {
        return ERR_NO_MEM;
    }

    err_t summary_err = session_store_write_summary(chat_id, summary);
    if (summary_err != 0) {
        pr_warn("Failed to write session summary for %s: %s", chat_id, err_name(summary_err));
    }

    char *facts = generate_facts_with_llm(messages, start_idx, end_idx);
    if (facts && facts[0]) {
        err_t facts_err = session_store_merge_facts(chat_id, facts);
        if (facts_err != 0) {
            pr_warn("Failed to merge session facts for %s: %s", chat_id, err_name(facts_err));
        }
    }
    kfree(facts);

    cJSON *compressed = build_compacted_messages(messages, n, messages, cfg, summary);
    kfree(summary);
    if (!compressed) {
        return ERR_NO_MEM;
    }

    err_t err = session_store_rewrite_from_array(chat_id, compressed);
    if (err != 0) {
        cJSON_Delete(compressed);
        return err;
    }

    cJSON_Delete(messages);
    *messages_io = compressed;

    pr_info("Compressed session %s: %d -> %d messages", chat_id, n, cJSON_GetArraySize(compressed));
    return 0;
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

        err_t summary_err = session_store_write_summary(chat_id, summary);
        if (summary_err != 0) {
            pr_warn("Failed to write session summary for %s: %s", chat_id, err_name(summary_err));
        }

        char *facts = generate_facts_with_llm(snapshot, start_idx, end_idx);
        if (facts && facts[0]) {
            err_t facts_err = session_store_merge_facts(chat_id, facts);
            if (facts_err != 0) {
                pr_warn("Failed to merge session facts for %s: %s", chat_id, err_name(facts_err));
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

        err_t rewrite_err = session_store_rewrite_from_array(chat_id, compressed);
        if (rewrite_err != 0) {
            pr_warn("Background compression rewrite failed for %s: %s", chat_id, err_name(rewrite_err));
            cJSON_Delete(compressed);
            cJSON_Delete(latest);
            cJSON_Delete(snapshot);
            return;
        }

        pr_info("Background compressed session %s: %d -> %d messages", chat_id, snapshot_count, cJSON_GetArraySize(compressed));

        cJSON_Delete(compressed);
        cJSON_Delete(latest);
        cJSON_Delete(snapshot);
    }
}
