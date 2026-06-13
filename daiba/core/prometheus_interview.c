#include "core/prometheus_interview.h"

#include "cJSON.h"
#include "core/config.h"
#include "drivers/llm/llm_proxy.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool prometheus_contains_any(const char *text, const char *const *needles, size_t needle_count)
{
    if (!text) {
        return false;
    }

    for (size_t i = 0; i < needle_count; i++) {
        if (strstr(text, needles[i])) {
            return true;
        }
    }
    return false;
}

static size_t prometheus_utf8_char_count(const char *text)
{
    if (!text) {
        return 0;
    }

    size_t count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

static bool prometheus_has_file_or_path(const char *text)
{
    static const char *const extensions[] = {
        ".c", ".h", ".py", ".js", ".ts", ".tsx", ".jsx", ".json", ".md",
        ".cmake", ".sh", ".go", ".rs", ".java", ".html", ".css", "CMakeLists.txt",
    };

    return prometheus_contains_any(text, extensions, sizeof(extensions) / sizeof(extensions[0])) ||
           strchr(text, '/') != NULL;
}

static bool prometheus_has_tech_stack(const char *text)
{
    static const char *const tech_terms[] = {
        "C11", "FreeRTOS", "Linux", "MIPS", "ARM", "OpenAI", "Anthropic", "LLM",
        "WebSocket", "HTTP", "JSON", "cJSON", "CMake", "Makefile", "Prometheus",
        "Agent", "IMPLEMENT", "API", "单元测试", "测试", "构建", "配置", "宏",
    };

    return prometheus_contains_any(text, tech_terms, sizeof(tech_terms) / sizeof(tech_terms[0]));
}

static bool prometheus_has_quantity_requirement(const char *text)
{
    if (!text) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (isdigit(*p)) {
            return true;
        }
    }

    static const char *const quantity_terms[] = {
        "一个", "两个", "三个", "2-3", "至少", "最多", "不少于", "数量", "每行",
    };
    return prometheus_contains_any(text, quantity_terms, sizeof(quantity_terms) / sizeof(quantity_terms[0]));
}

static bool prometheus_message_is_specific(const char *user_message)
{
    if (!user_message || !user_message[0]) {
        return false;
    }

    if (prometheus_utf8_char_count(user_message) < 20) {
        return false;
    }

    return prometheus_has_file_or_path(user_message) ||
           prometheus_has_tech_stack(user_message) ||
           prometheus_has_quantity_requirement(user_message);
}

static void prometheus_fallback_questions(const char *user_message, char *out, size_t out_size)
{
    (void)user_message;
    if (!out || out_size == 0) {
        return;
    }

    snprintf(out,
             out_size,
             "你想具体实现或修改哪个功能/模块？\n"
             "有没有指定文件、技术栈或接口约束？\n"
             "完成后你希望用什么结果或测试来验收？");
    out[out_size - 1] = '\0';
}

static bool prometheus_llm_reply_is_specific(const char *text)
{
    if (!text) {
        return false;
    }

    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    return strncmp(text, "SPECIFIC", 8) == 0;
}

static daima_err_t prometheus_generate_questions_with_llm(const char *user_message,
                                                          char *out,
                                                          size_t out_size)
{
    if (!out || out_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char prompt[4096];
    int n = snprintf(prompt,
                     sizeof(prompt),
                     "用户想要: %s\n\n"
                     "这个需求够具体吗？如果不够，请生成2-3个简短的澄清问题。\n"
                     "只需要回复问题本身，每行一个，不要有其他内容。\n"
                     "如果已经够具体，只回复\"SPECIFIC\"。",
                     user_message ? user_message : "");
    if (n < 0 || (size_t)n >= sizeof(prompt)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *messages = cJSON_CreateArray();
    cJSON *user = cJSON_CreateObject();
    if (!messages || !user) {
        cJSON_Delete(messages);
        cJSON_Delete(user);
        return DAIMA_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", prompt);
    cJSON_AddItemToArray(messages, user);

    llm_response_t resp = {0};
    daima_err_t err = llm_chat_tools("你是 Prometheus Interview Mode，负责在执行前澄清模糊需求。",
                                     messages,
                                     NULL,
                                     &resp);
    cJSON_Delete(messages);
    if (err != DAIMA_OK) {
        llm_response_free(&resp);
        return err;
    }

    if (!resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return DAIMA_ERR_INVALID_ARG;
    }

    snprintf(out, out_size, "%s", resp.text);
    out[out_size - 1] = '\0';
    llm_response_free(&resp);
    return DAIMA_OK;
}

daima_err_t prometheus_check_needs_interview(const char *user_message,
                                             prometheus_state_t *out)
{
    if (!user_message || !out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->enabled = true;

#if !DAIMA_PROMETHEUS_INTERVIEW_ENABLED
    return DAIMA_OK;
#else
    if (prometheus_message_is_specific(user_message)) {
        snprintf(out->questions, sizeof(out->questions), "SPECIFIC");
        out->needs_interview = false;
        return DAIMA_OK;
    }

    daima_err_t err = prometheus_generate_questions_with_llm(user_message,
                                                            out->questions,
                                                            sizeof(out->questions));
    if (err != DAIMA_OK) {
        prometheus_fallback_questions(user_message, out->questions, sizeof(out->questions));
    }

    out->needs_interview = !prometheus_llm_reply_is_specific(out->questions);
    if (!out->needs_interview) {
        snprintf(out->questions, sizeof(out->questions), "SPECIFIC");
    }
    return DAIMA_OK;
#endif
}
