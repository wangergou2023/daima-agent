#include "intent.h"

#include "autoconf.h"
#include "env.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"
#include "cJSON.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"

static const char *TAG = "intent_gate";

#ifndef DAIMA_INTENT_GATE_ENABLED
#define DAIMA_INTENT_GATE_ENABLED 1
#endif
#ifndef DAIMA_INTENT_GATE_LLM_FALLBACK
#define DAIMA_INTENT_GATE_LLM_FALLBACK 1
#endif

#if DAIMA_INTENT_GATE_LLM_FALLBACK
static daima_err_t intent_gate_classify_llm(const char *user_message,
                                             daima_intent_t *out_intent);
#endif

typedef struct {
    daima_intent_t intent;
    const char *const *keywords;
} intent_keyword_group_t;

static const char *const FIX_KEYWORDS[] = {
    "修复", "修", "bug", "报错", "错误", "失败", "fix", "error", NULL,
};

static const char *const IMPLEMENT_KEYWORDS[] = {
    "实现", "添加", "创建", "写一个", "写个", "帮我写", "开发", "build", "add", NULL,
};

static const char *const INVESTIGATE_KEYWORDS[] = {
    "调研", "分析", "查看", "检查", "找一下", "搜索", "有哪些", NULL,
};

static const char *const QA_KEYWORDS[] = {
    "是什么", "怎么用", "解释", "什么是", "什么意思", "介绍一下", NULL,
};

static const intent_keyword_group_t KEYWORD_GROUPS[] = {
    { DAIMA_INTENT_FIX, FIX_KEYWORDS },
    { DAIMA_INTENT_IMPLEMENT, IMPLEMENT_KEYWORDS },
    { DAIMA_INTENT_INVESTIGATE, INVESTIGATE_KEYWORDS },
    { DAIMA_INTENT_QA, QA_KEYWORDS },
};

const char *daima_intent_name(daima_intent_t intent)
{
    switch (intent) {
    case DAIMA_INTENT_QA:
        return "QA";
    case DAIMA_INTENT_IMPLEMENT:
        return "IMPLEMENT";
    case DAIMA_INTENT_INVESTIGATE:
        return "INVESTIGATE";
    case DAIMA_INTENT_FIX:
        return "FIX";
    case DAIMA_INTENT_OPEN:
        return "OPEN";
    default:
        return "UNKNOWN";
    }
}

intent_gate_cfg_t intent_gate_load_cfg(void)
{
    intent_gate_cfg_t cfg = {
        .enabled = daima_env_bool_or_default("DAIMA_INTENT_GATE_ENABLED",
                                             DAIMA_INTENT_GATE_ENABLED != 0),
    };
    return cfg;
}

static char *ascii_lower_copy(const char *text)
{
    size_t len = strlen(text);
    char *copy = (char *)kmalloc(len + 1, GFP_KERNEL);
    if (!copy) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        copy[i] = (char)(ch < 0x80 ? tolower(ch) : ch);
    }
    copy[len] = '\0';
    return copy;
}

static bool contains_keyword(const char *lower_message, const char *keyword)
{
    char *lower_keyword = ascii_lower_copy(keyword);
    if (!lower_keyword) {
        return strstr(lower_message, keyword) != NULL;
    }

    bool matched = strstr(lower_message, lower_keyword) != NULL;
    kfree(lower_keyword);
    return matched;
}

daima_err_t intent_gate_classify(const char *user_message,
                                  daima_intent_t *out_intent)
{
    if (!user_message || !out_intent) {
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_intent_t intent = DAIMA_INTENT_OPEN;
    char *lower_message = ascii_lower_copy(user_message);
    const char *message_to_scan = lower_message ? lower_message : user_message;

    for (size_t i = 0; i < (sizeof(KEYWORD_GROUPS) / sizeof(KEYWORD_GROUPS[0])); i++) {
        const char *const *keywords = KEYWORD_GROUPS[i].keywords;
        for (size_t j = 0; keywords[j] != NULL; j++) {
            if (contains_keyword(message_to_scan, keywords[j])) {
                intent = KEYWORD_GROUPS[i].intent;
                goto done;
            }
        }
    }

done:
    kfree(lower_message);
    *out_intent = intent;

#if DAIMA_INTENT_GATE_LLM_FALLBACK
    if (intent == DAIMA_INTENT_OPEN) {
        intent_gate_classify_llm(user_message, out_intent);
    }
#endif

    DAIMA_LOGI(TAG, "IntentGate classified: %s", daima_intent_name(*out_intent));
    return DAIMA_OK;
}

#if DAIMA_INTENT_GATE_LLM_FALLBACK
static daima_err_t intent_gate_classify_llm(const char *user_message,
                                             daima_intent_t *out_intent)
{
    char classify_prompt[1024];
    snprintf(classify_prompt, sizeof(classify_prompt),
        "Classify this user request into exactly one category. Reply with only the label, nothing else.\n"
        "Categories:\n"
        "- IMPLEMENT: building, creating, writing code, adding features\n"
        "- FIX: debugging, fixing errors, repairing bugs\n"
        "- INVESTIGATE: researching, analyzing, searching, exploring\n"
        "- QA: asking what/how/why questions, requesting explanations\n"
        "- OPEN: anything else\n"
        "\n"
        "Request: %s\n"
        "\n"
        "Label:",
        user_message);

    cJSON *messages = cJSON_CreateArray();
    cJSON *msg_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_obj, "role", "user");
    cJSON_AddStringToObject(msg_obj, "content", classify_prompt);
    cJSON_AddItemToArray(messages, msg_obj);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    daima_err_t err = llm_chat_tools(classify_prompt, messages, NULL, &resp);

    if (err == DAIMA_OK && resp.text && resp.text[0]) {
        if (strstr(resp.text, "IMPLEMENT"))      *out_intent = DAIMA_INTENT_IMPLEMENT;
        else if (strstr(resp.text, "FIX"))        *out_intent = DAIMA_INTENT_FIX;
        else if (strstr(resp.text, "INVESTIGATE")) *out_intent = DAIMA_INTENT_INVESTIGATE;
        else if (strstr(resp.text, "QA"))          *out_intent = DAIMA_INTENT_QA;
        DAIMA_LOGI(TAG, "IntentGate LLM reclassified: %s -> %s (raw: %.80s)",
                   user_message, daima_intent_name(*out_intent), resp.text);
    }

    llm_response_free(&resp);
    cJSON_Delete(messages);
    return err;
}
#endif
