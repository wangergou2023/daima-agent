#include "agent/intent_gate.h"

#include "daima_config.h"
#include "daima_env.h"
#include "daima_log.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "intent_gate";

#ifndef DAIMA_INTENT_GATE_ENABLED
#define DAIMA_INTENT_GATE_ENABLED 1
#endif

typedef struct {
    daima_intent_t intent;
    const char *const *keywords;
} intent_keyword_group_t;

static const char *const FIX_KEYWORDS[] = {
    "修复", "修", "bug", "报错", "错误", "失败", "fix", "error", NULL,
};

static const char *const IMPLEMENT_KEYWORDS[] = {
    "实现", "添加", "创建", "写一个", "开发", "build", "add", NULL,
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
    char *copy = (char *)malloc(len + 1);
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
    free(lower_keyword);
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
    free(lower_message);
    *out_intent = intent;
    DAIMA_LOGI(TAG, "IntentGate classified: %s", daima_intent_name(intent));
    return DAIMA_OK;
}
