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
#ifndef INTENT_GATE_ENABLED
#define INTENT_GATE_ENABLED 1
#endif
#ifndef INTENT_GATE_LLM_FALLBACK
#define INTENT_GATE_LLM_FALLBACK 1
#endif

#if INTENT_GATE_LLM_FALLBACK
static err_t intent_gate_classify_llm(const char *user_message,
                                             enum intent *out_intent);
#endif

typedef struct {
    enum intent intent;
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
    { INTENT_FIX, FIX_KEYWORDS },
    { INTENT_IMPLEMENT, IMPLEMENT_KEYWORDS },
    { INTENT_INVESTIGATE, INVESTIGATE_KEYWORDS },
    { INTENT_QA, QA_KEYWORDS },
};

const char *intent_name(enum intent intent)
{
    switch (intent) {
    case INTENT_QA:
        return "QA";
    case INTENT_IMPLEMENT:
        return "IMPLEMENT";
    case INTENT_INVESTIGATE:
        return "INVESTIGATE";
    case INTENT_FIX:
        return "FIX";
    case INTENT_OPEN:
        return "OPEN";
    default:
        return "UNKNOWN";
    }
}

intent_gate_cfg_t intent_gate_load_cfg(void)
{
    intent_gate_cfg_t cfg = {
        .enabled = env_bool_or_default("INTENT_GATE_ENABLED",
                                             INTENT_GATE_ENABLED != 0),
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

err_t intent_gate_classify(const char *user_message,
                                  enum intent *out_intent)
{
    if (!user_message || !out_intent) {
        return ERR_INVALID_ARG;
    }

    enum intent intent = INTENT_OPEN;
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

#if INTENT_GATE_LLM_FALLBACK
    if (intent == INTENT_OPEN) {
        intent_gate_classify_llm(user_message, out_intent);
    }
#endif

    pr_info("IntentGate classified: %s", intent_name(*out_intent));
    return 0;
}

#if INTENT_GATE_LLM_FALLBACK
static err_t intent_gate_classify_llm(const char *user_message,
                                             enum intent *out_intent)
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
    err_t err = llm_chat_tools(classify_prompt, messages, NULL, &resp);

    if (err == 0 && resp.text && resp.text[0]) {
        if (strstr(resp.text, "IMPLEMENT"))      *out_intent = INTENT_IMPLEMENT;
        else if (strstr(resp.text, "FIX"))        *out_intent = INTENT_FIX;
        else if (strstr(resp.text, "INVESTIGATE")) *out_intent = INTENT_INVESTIGATE;
        else if (strstr(resp.text, "QA"))          *out_intent = INTENT_QA;
        pr_info("IntentGate LLM reclassified: %s -> %s (raw: %.80s)", user_message, intent_name(*out_intent), resp.text);
    }

    llm_response_free(&resp);
    cJSON_Delete(messages);
    return err;
}
#endif
