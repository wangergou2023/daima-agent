/* Prometheus 面试模式：在 IMPLEMENT 前检查需求是否足够具体。
 * 若模糊（<20字符 或无文件路径/技术栈/数量约束），通过 LLM 生成 2-3 个澄清问题。 */

#include "interview.h"

#include "cjson.h"
#include "autoconf.h"
#include "drivers/llm/llm_proxy.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"
#include "linux/printk.h"

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

static bool prometheus_has_explicit_file_target(const char *text)
{
    static const char *const file_markers[] = {
        ".c", ".h", ".py", ".js", ".ts", ".tsx", ".jsx", ".json", ".md",
        ".cmake", ".sh", ".go", ".rs", ".java", ".html", ".css", "CMakeLists.txt",
    };

    return prometheus_contains_any(text, file_markers, sizeof(file_markers) / sizeof(file_markers[0]));
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

static bool prometheus_has_explicit_target_verb(const char *text)
{
    static const char *const target_verbs[] = {
        "修改", "改", "改成", "更新", "重构", "实现", "补", "增加", "新增", "删除",
        "replace", "change", "update", "refactor", "implement", "add", "remove",
    };

    return prometheus_contains_any(text, target_verbs, sizeof(target_verbs) / sizeof(target_verbs[0]));
}

static bool prometheus_has_scope_anchor(const char *text)
{
    static const char *const scope_terms[] = {
        "只", "仅", "限定", "重点", "围绕", "针对", "聚焦", "模块", "文件", "函数",
        "class", "function", "module", "file", "only", "focus", "scope",
    };

    return prometheus_contains_any(text, scope_terms, sizeof(scope_terms) / sizeof(scope_terms[0]));
}

static bool prometheus_has_uncertain_scope(const char *text)
{
    static const char *const uncertain_scope_terms[] = {
        "没想好", "还没想好", "还没决定", "哪个模块", "哪个文件", "哪个目录",
        "not sure which", "haven't decided", "not decided",
    };

    return prometheus_contains_any(text, uncertain_scope_terms,
                                   sizeof(uncertain_scope_terms) / sizeof(uncertain_scope_terms[0]));
}

static bool prometheus_requests_start_without_scope(const char *text)
{
    static const char *const vague_markers[] = {
        "先直接开始", "你先开始", "先做起来", "先弄", "先改", "随便改", "帮我改一下",
        "i haven't decided", "not sure", "just start", "start directly", "figure it out",
    };

    return prometheus_contains_any(text, vague_markers, sizeof(vague_markers) / sizeof(vague_markers[0]));
}

static bool prometheus_must_force_interview(const char *user_message)
{
    if (!user_message || !user_message[0]) {
        return false;
    }

    if (prometheus_requests_start_without_scope(user_message)) {
        return true;
    }

    if (prometheus_has_explicit_target_verb(user_message) &&
        (prometheus_has_file_or_path(user_message) || prometheus_has_tech_stack(user_message)) &&
        !prometheus_has_scope_anchor(user_message) &&
        !prometheus_has_quantity_requirement(user_message)) {
        return true;
    }

    return false;
}

/** 检查消息是否足够具体：
 *  1. 至少 20 个 Unicode 字符
 *  2. 不能是“先开始/没想好改哪”的明显模糊实现请求
 *  3. 必须同时给出足够的目标锚点，而不是只有路径/技术名词 */
static bool prometheus_message_is_specific(const char *user_message)
{
    if (!user_message || !user_message[0]) {
        return false;
    }

    if (prometheus_utf8_char_count(user_message) < 20) {
        return false;
    }

    if (prometheus_requests_start_without_scope(user_message)) {
        return false;
    }
    if (prometheus_has_uncertain_scope(user_message)) {
        return false;
    }

    bool has_path = prometheus_has_file_or_path(user_message);
    bool has_file_target = prometheus_has_explicit_file_target(user_message);
    bool has_stack = prometheus_has_tech_stack(user_message);
    bool has_quantity = prometheus_has_quantity_requirement(user_message);
    bool has_target_verb = prometheus_has_explicit_target_verb(user_message);
    bool has_scope_anchor = prometheus_has_scope_anchor(user_message) &&
                            !prometheus_has_uncertain_scope(user_message);

    if (has_quantity && (has_path || has_stack || has_scope_anchor)) {
        return true;
    }

    if (has_path && has_target_verb && has_scope_anchor) {
        return true;
    }

    if (has_file_target && has_target_verb) {
        return true;
    }

    if (has_stack && has_target_verb && (has_scope_anchor || has_quantity)) {
        return true;
    }

    return false;
}

/** 不适合 LLM 时使用的硬编码 fallback 澄清问题。 */
static void prometheus_fallback_questions(const char *user_message, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    if (prometheus_requests_start_without_scope(user_message)) {
        snprintf(out,
                 out_size,
                 "你准备先改哪个具体模块或目录？\n"
                 "这次是要做架构分析、功能实现，还是问题修复？\n"
                 "本轮先产出什么结果最合适：分析结论、代码修改，还是验证脚本？");
        out[out_size - 1] = '\0';
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

/** 通过 LLM 生成澄清问题。若 LLM 回复 SPECIFIC 则认为需求已足够具体。 */
static err_t prometheus_generate_questions_with_llm(const char *user_message,
                                                          char *out,
                                                          size_t out_size)
{
    if (!out || out_size == 0) {
        return ERR_INVALID_ARG;
    }

    if (prometheus_must_force_interview(user_message)) {
        prometheus_fallback_questions(user_message, out, out_size);
        return 0;
    }

    char prompt[4096];
    int n = snprintf(prompt,
                     sizeof(prompt),
                     "用户想要: %s\n\n"
                     "这个需求是否已经足够具体到可以直接开始改代码？\n"
                     "如果用户还没决定改哪个模块、只说先开始、或只是给了仓库路径/技术背景但没有明确范围，必须视为不具体。\n"
                     "这种情况下绝对不要回复 SPECIFIC，必须生成 2-3 个简短澄清问题。\n"
                     "只有在修改范围、目标模块/文件、以及本轮目标都已经明确时，才允许回复 SPECIFIC。\n"
                     "只需要回复问题本身，每行一个，不要有其他内容。\n"
                     "如果已经够具体，只回复\"SPECIFIC\"。",
                     user_message ? user_message : "");
    if (n < 0 || (size_t)n >= sizeof(prompt)) {
        return ERR_INVALID_ARG;
    }

    cJSON *messages = cJSON_CreateArray();
    cJSON *user = cJSON_CreateObject();
    if (!messages || !user) {
        cJSON_Delete(messages);
        cJSON_Delete(user);
        return ERR_NO_MEM;
    }

    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", prompt);
    cJSON_AddItemToArray(messages, user);

    llm_response_t resp = {0};
    err_t err = llm_chat_tools("你是 Prometheus Interview Mode，负责在执行前澄清模糊需求。",
                                     messages,
                                     NULL,
                                     &resp);
    cJSON_Delete(messages);
    if (err != 0) {
        llm_response_free(&resp);
        return err;
    }

    if (!resp.text || !resp.text[0]) {
        llm_response_free(&resp);
        return ERR_INVALID_ARG;
    }

    strscpy(out, resp.text, out_size);
    out[out_size - 1] = '\0';
    llm_response_free(&resp);
    return 0;
}

/** Prometheus 检查入口：判断需求是否需要面试澄清。
 *  @param out  输出：needs_interview=true（需澄清）或 questions="SPECIFIC"（已具体） */
err_t prometheus_check_needs_interview(const char *user_message,
                                             prometheus_state_t *out)
{
    if (!user_message || !out) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->enabled = true;

if (prometheus_must_force_interview(user_message)) {
        prometheus_fallback_questions(user_message, out->questions, sizeof(out->questions));
        out->needs_interview = true;
        pr_info("PrometheusInterview: forced needs_interview=1 message=%.160s questions=%.200s",
                user_message ? user_message : "",
                out->questions);
        return 0;
    }

    if (prometheus_message_is_specific(user_message)) {
        snprintf(out->questions, sizeof(out->questions), "SPECIFIC");
        out->needs_interview = false;
        pr_info("PrometheusInterview: specific needs_interview=0 message=%.160s",
                user_message ? user_message : "");
        return 0;
    }

    err_t err = prometheus_generate_questions_with_llm(user_message,
                                                            out->questions,
                                                            sizeof(out->questions));
    if (err != 0) {
        prometheus_fallback_questions(user_message, out->questions, sizeof(out->questions));
    }

    out->needs_interview = !prometheus_llm_reply_is_specific(out->questions);
    if (!out->needs_interview) {
        snprintf(out->questions, sizeof(out->questions), "SPECIFIC");
    }
    pr_info("PrometheusInterview: llm needs_interview=%d message=%.160s questions=%.200s",
            out->needs_interview ? 1 : 0,
            user_message ? user_message : "",
            out->questions);
    return 0;
}
