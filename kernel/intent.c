/* intent 分类：全 LLM 驱动，无关键词匹配 */
#include "intent.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"
#include "cjson.h"
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

static err_t intent_gate_classify_llm(const char *user_message,
                                       enum intent *out_intent)
{
    char prompt[1024];
    snprintf(prompt, sizeof(prompt),
        "Classify into one category. Reply with only the label.\n"
        "- IMPLEMENT: building, creating, writing code, adding features\n"
        "- FIX: debugging, fixing errors, repairing bugs\n"
        "- INVESTIGATE: researching, analyzing, searching, exploring\n"
        "- QA: asking what/how/why questions, requesting explanations\n"
        "- OPEN: anything else\n\nRequest: %s\n\nLabel:", user_message);

    cJSON *messages = cJSON_CreateArray();
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", "user");
    cJSON_AddStringToObject(obj, "content", prompt);
    cJSON_AddItemToArray(messages, obj);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    err_t err = llm_chat_tools(prompt, messages, NULL, &resp);

    if (err == 0 && resp.text && resp.text[0]) {
        if (strstr(resp.text, "IMPLEMENT"))      *out_intent = INTENT_IMPLEMENT;
        else if (strstr(resp.text, "FIX"))        *out_intent = INTENT_FIX;
        else if (strstr(resp.text, "INVESTIGATE")) *out_intent = INTENT_INVESTIGATE;
        else if (strstr(resp.text, "QA"))          *out_intent = INTENT_QA;
        pr_info("IntentGate LLM: %s → %.80s", intent_name(*out_intent), resp.text);
    }
    llm_response_free(&resp);
    cJSON_Delete(messages);
    return err;
}

err_t intent_gate_classify(const char *user_message, enum intent *out_intent)
{
    if (!user_message || !out_intent) return ERR_INVALID_ARG;
    *out_intent = INTENT_OPEN;
    intent_gate_classify_llm(user_message, out_intent);
    return 0;
}
