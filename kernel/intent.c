/* intent 分类：全 LLM 驱动，无关键词匹配 */
#include "intent.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#include "drivers/llm/llm_proxy.h"
#include "transcript.h"
#include "cjson.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "linux/slab.h"

const char *intent_name(enum intent intent)
{
    switch (intent) {
    case INTENT_QA:          return "QA";
    case INTENT_IMPLEMENT:   return "IMPLEMENT";
    case INTENT_INVESTIGATE: return "INVESTIGATE";
    case INTENT_FIX:         return "FIX";
    case INTENT_OPEN:        return "OPEN";
    default:                 return "UNKNOWN";
    }
}

static bool intent_text_looks_like_action_request(const char *user_message)
{
    static const char *const action_markers[] = {
        "实现", "修", "修复", "修改", "改一下", "重构", "加一个", "新增", "添加",
        "写一个", "做一个", "处理一下", "支持", "优化", "完善",
        "implement", "build", "create", "write", "add", "fix", "repair",
        "refactor", "update", "modify", "support", "improve"
    };

    if (!user_message || !user_message[0]) {
        return false;
    }
    if (strchr(user_message, '?') || strstr(user_message, "？")) {
        return false;
    }

    for (size_t i = 0; i < sizeof(action_markers) / sizeof(action_markers[0]); i++) {
        if (strstr(user_message, action_markers[i])) {
            return true;
        }
    }
    return false;
}

bool intent_gate_text_looks_like_action_request_for_test(const char *user_message)
{
    return intent_text_looks_like_action_request(user_message);
}

enum intent intent_gate_fallback_for_text(const char *user_message)
{
    if (!user_message || !user_message[0]) {
        return INTENT_OPEN;
    }
    if (intent_text_looks_like_action_request(user_message)) {
        return INTENT_IMPLEMENT;
    }
    return INTENT_OPEN;
}

static void intent_assign_from_label(const char *label,
                                     const char *task_mode,
                                     const char *user_message,
                                     enum intent *out_intent)
{
    if (!out_intent) {
        return;
    }

    if (label && strstr(label, "IMPLEMENT")) {
        *out_intent = INTENT_IMPLEMENT;
        return;
    }
    if (label && strstr(label, "FIX")) {
        *out_intent = INTENT_FIX;
        return;
    }
    if (label && strstr(label, "INVESTIGATE")) {
        *out_intent = INTENT_INVESTIGATE;
        return;
    }
    if (label && strstr(label, "QA")) {
        *out_intent = INTENT_QA;
    } else if (label && strstr(label, "OPEN")) {
        *out_intent = INTENT_OPEN;
    }

    if ((task_mode && strstr(task_mode, "action")) ||
        intent_text_looks_like_action_request(user_message)) {
        if (*out_intent == INTENT_QA || *out_intent == INTENT_OPEN) {
            *out_intent = INTENT_IMPLEMENT;
        }
    }
}

static err_t intent_gate_classify_llm(const char *user_message,
                                       enum intent *out_intent)
{
    char prompt[1536];
    snprintf(prompt, sizeof(prompt),
        "Classify the request into one intent and return a JSON object only.\n"
        "Schema: {\"intent\":\"IMPLEMENT|FIX|INVESTIGATE|QA|OPEN\",\"task_mode\":\"action|question|other\",\"needs_clarification\":true|false}\n"
        "Rules:\n"
        "- IMPLEMENT: the user wants the agent to build, create, write, modify, add, or implement something.\n"
        "- FIX: the user wants the agent to debug, repair, or fix an existing problem.\n"
        "- INVESTIGATE: the user wants analysis, exploration, architecture review, or discovery.\n"
        "- QA: the user is primarily asking for explanation or conceptual answer.\n"
        "- OPEN: anything else.\n"
        "- If the user is asking the agent to do work but the request is underspecified, still choose IMPLEMENT or FIX, not QA.\n"
        "- Ambiguity about missing details should set needs_clarification=true, but must not flip an action request into QA.\n"
        "Request: %s",
        user_message ? user_message : "");

    cJSON *messages = cJSON_CreateArray();
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", "user");
    cJSON_AddStringToObject(obj, "content", prompt);
    cJSON_AddItemToArray(messages, obj);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    err_t err = llm_chat_tools_with_model_and_format(prompt, messages, NULL, NULL, true, &resp);

    if (err == 0 && resp.text && resp.text[0]) {
        cJSON *root = cJSON_Parse(resp.text);
        const char *label = NULL;
        const char *task_mode = NULL;
        if (root && cJSON_IsObject(root)) {
            label = cJSON_GetStringValue(cJSON_GetObjectItem(root, "intent"));
            task_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "task_mode"));
        }
        intent_assign_from_label(label ? label : resp.text,
                                 task_mode,
                                 user_message,
                                 out_intent);
        cJSON_Delete(root);
        pr_info("IntentGate LLM: %s → %.80s", intent_name(*out_intent), resp.text);
    }
    llm_response_free(&resp);
    cJSON_Delete(messages);
    return err;
}

err_t intent_gate_classify(const char *user_message, enum intent *out_intent)
{
    if (!user_message || !out_intent) return ERR_INVALID_ARG;
    *out_intent = intent_gate_fallback_for_text(user_message);
    err_t err = intent_gate_classify_llm(user_message, out_intent);
    if (err != 0) {
        pr_warn("IntentGate fallback: err=%s intent=%s request=%.120s",
                err_name(err),
                intent_name(*out_intent),
                user_message);
    }
    return 0;
}

err_t intent_gate_analyze_task(const char *user_message,
                               task_analysis_t *out_analysis)
{
    if (!user_message || !out_analysis)
        return ERR_INVALID_ARG;

    memset(out_analysis, 0, sizeof(*out_analysis));

    /* 1. 意图分类 */
    enum intent intent_type;
    err_t err = intent_gate_classify(user_message, &intent_type);
    if (err != 0)
        intent_type = intent_gate_fallback_for_text(user_message);
    out_analysis->intent_type = intent_type;

    /* 2. 能力标签提取（仅基于用户消息） */
    transcript_extract_skill_tags(user_message, NULL,
                                  out_analysis->capability_tags,
                                  sizeof(out_analysis->capability_tags));

    /* 3. 启发式约束 */
    out_analysis->requires_file_write =
        (intent_type == INTENT_IMPLEMENT || intent_type == INTENT_FIX);
    out_analysis->requires_network =
        (strstr(user_message, "http") || strstr(user_message, "api") ||
         strstr(user_message, "fetch") || strstr(user_message, "curl"));

    /* 4. 复杂度估算 */
    size_t len = strlen(user_message);
    if (len < 50)
        out_analysis->estimated_complexity = 1;
    else if (len < 200)
        out_analysis->estimated_complexity = 2;
    else
        out_analysis->estimated_complexity = 3;

    out_analysis->confidence = (intent_type != INTENT_QA) ? 0.7f : 0.5f;

    return 0;
}
