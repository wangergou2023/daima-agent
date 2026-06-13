/* Host 侧 LLM 代理实现：基于 libcurl 的 HTTP 请求 */
#include "drivers/llm/llm_proxy.h"
#include "core/runtime.h"
#include "drivers/llm/llm_anthropic_payload.h"
#include "drivers/llm/llm_openai_payload.h"
#include "arch/host/llm_http_client_host.h"
#include "core/base64.h"
#include "core/text.h"
#include "core/paths.h"
#include "core/config.h"
#include "core/http.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

/* macOS lacks memrchr (GNU extension) */
#ifndef __linux__
static void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) {
        p--;
        if (*p == (unsigned char)c) return (void *)p;
    }
    return NULL;
}
#endif
#include <stdlib.h>
#include "core/log.h"
#include "cJSON.h"
#include "core/utils/json_helpers.h"

static const char *TAG = "llm";
static const char *DEFAULT_LLM_MODEL = "kimi-k2.5";
static const int DEFAULT_CONTEXT_LIMIT_TOKENS = 128000;

/* 运行期配置缓存（由 config.json 注入） */
#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
#define LLM_AUTH_HEADER_MAX 352
static char s_api_key[LLM_API_KEY_MAX_LEN] = {0};
static char s_model[LLM_MODEL_MAX_LEN] = "kimi-k2.5";
static char s_openai_base_url[DAIMA_BUF_SMALL] = {0};
static char s_openai_api_url[DAIMA_BUF_MEDIUM] = {0};
static bool s_use_anthropic_api = false;
static bool s_api_key_set = false;
static bool s_model_set = false;
static unsigned s_llm_debug_seq = 0;

struct llm_async_chat {
    llm_async_request_t *http_req;
    cJSON *messages_ref;
    char tools_json_buf[4096];
    char system_prompt_buf[2048];
    char model_name[64];
    char *post_data;
    struct curl_slist *headers;
    bool launched;
    bool use_anthropic_api;
};

static void ensure_dir_path(const char *path)
{
    if (!path || !path[0]) {
        return;
    }
    char tmp[DAIMA_BUF_MEDIUM];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static bool object_is_empty(cJSON *obj)
{
    return cJSON_IsObject(obj) && obj->child == NULL;
}

static void log_llm_response_diagnostics(const char *protocol,
                                         const char *raw_resp,
                                         cJSON *root)
{
    if (!root) {
        return;
    }

    const char *id = json_string(root, "id");
    const char *model = json_string(root, "model");
    const char *stop = json_string(root, "stop_reason");
    if (!stop) {
        stop = json_string(root, "finish_reason");
    }

    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    int input_tokens = usage ? json_number(usage, "input_tokens") : -1;
    int output_tokens = usage ? json_number(usage, "output_tokens") : -1;
    int total_tokens = usage ? json_number(usage, "total_tokens") : -1;
    if (input_tokens < 0 && usage) input_tokens = json_number(usage, "prompt_tokens");
    if (output_tokens < 0 && usage) output_tokens = json_number(usage, "completion_tokens");

    int text_blocks = 0;
    int thinking_blocks = 0;
    int tool_blocks = 0;
    int empty_tool_inputs = 0;
    int max_tool_input_len = 0;
    char empty_tool_name[96] = "";

    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        cJSON *block = NULL;
        cJSON_ArrayForEach(block, content) {
            const char *type = json_string(block, "type");
            if (!type) {
                continue;
            }
            if (strcmp(type, "text") == 0) {
                text_blocks++;
            } else if (strcmp(type, "thinking") == 0 || strcmp(type, "reasoning") == 0) {
                thinking_blocks++;
            } else if (strcmp(type, "tool_use") == 0) {
                tool_blocks++;
                cJSON *input = cJSON_GetObjectItem(block, "input");
                char *input_json = input ? cJSON_PrintUnformatted(input) : NULL;
                int input_len = input_json ? (int)strlen(input_json) : -1;
                if (input_len > max_tool_input_len) {
                    max_tool_input_len = input_len;
                }
                if (object_is_empty(input)) {
                    empty_tool_inputs++;
                    const char *name = json_string(block, "name");
                    if (name && !empty_tool_name[0]) {
                        snprintf(empty_tool_name, sizeof(empty_tool_name), "%s", name);
                    }
                }
                free(input_json);
            }
        }
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices)) {
        cJSON *choice = NULL;
        cJSON_ArrayForEach(choice, choices) {
            const char *finish = json_string(choice, "finish_reason");
            if (finish) {
                stop = finish;
            }
            cJSON *msg = cJSON_GetObjectItem(choice, "message");
            cJSON *tool_calls = msg ? cJSON_GetObjectItem(msg, "tool_calls") : NULL;
            if (tool_calls && cJSON_IsArray(tool_calls)) {
                cJSON *tc = NULL;
                cJSON_ArrayForEach(tc, tool_calls) {
                    tool_blocks++;
                    cJSON *func = cJSON_GetObjectItem(tc, "function");
                    const char *args = func ? json_string(func, "arguments") : NULL;
                    int input_len = args ? (int)strlen(args) : -1;
                    if (input_len > max_tool_input_len) {
                        max_tool_input_len = input_len;
                    }
                    if (args && strcmp(args, "{}") == 0) {
                        empty_tool_inputs++;
                        const char *name = func ? json_string(func, "name") : NULL;
                        if (name && !empty_tool_name[0]) {
                            snprintf(empty_tool_name, sizeof(empty_tool_name), "%s", name);
                        }
                    }
                }
            }
        }
    }

    DAIMA_LOGI(TAG,
               "LLM diagnostics: protocol=%s id=%s model=%s stop=%s usage_in=%d usage_out=%d usage_total=%d raw_bytes=%d blocks{text=%d thinking=%d tool=%d} max_tool_input_len=%d empty_tool_inputs=%d%s%s",
               protocol ? protocol : "-",
               id ? id : "-",
               model ? model : "-",
               stop ? stop : "-",
               input_tokens,
               output_tokens,
               total_tokens,
               raw_resp ? (int)strlen(raw_resp) : 0,
               text_blocks,
               thinking_blocks,
               tool_blocks,
               max_tool_input_len,
               empty_tool_inputs,
               empty_tool_name[0] ? " first_empty_tool=" : "",
               empty_tool_name[0] ? empty_tool_name : "");

    if (empty_tool_inputs <= 0 || !raw_resp) {
        return;
    }

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/llm_debug", daima_path_cache_dir());
    ensure_dir_path(dir);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);

    char path[768];
    snprintf(path, sizeof(path),
             "%s/empty-tool-input-%s-%03ld-%u.json",
             dir,
             ts,
             tv.tv_usec / 1000,
             ++s_llm_debug_seq);

    FILE *f = fopen(path, "w");
    if (!f) {
        DAIMA_LOGW(TAG, "Failed to write LLM empty tool debug file: %s", path);
        return;
    }
    fwrite(raw_resp, 1, strlen(raw_resp), f);
    fclose(f);
    DAIMA_LOGW(TAG, "LLM empty tool input raw response saved: %s", path);
}

static bool url_tail_is_version_root(const char *url)
{
    if (!url || !url[0]) {
        return false;
    }

    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        len--;
    }
    if (len == 0) {
        return false;
    }

    const char *tail = memrchr(url, '/', len);
    tail = tail ? tail + 1 : url;
    if ((size_t)(tail - url) >= len || !tail[0]) {
        return false;
    }

    if (tail[0] != 'v' && tail[0] != 'V') {
        return false;
    }

    const char *p = tail + 1;
    bool has_digit = false;
    while ((size_t)(p - url) < len) {
        if (*p >= '0' && *p <= '9') {
            has_digit = true;
            p++;
            continue;
        }
        if (*p == '.') {
            p++;
            continue;
        }
        return false;
    }
    return has_digit;
}

static bool base_url_is_deepseek_official(const char *url)
{
    return url && strstr(url, "api.deepseek.com") != NULL;
}

static bool api_mode_is_anthropic_messages(const char *api_mode)
{
    return api_mode &&
           (strcasecmp(api_mode, "anthropic_messages") == 0 ||
            strcasecmp(api_mode, "anthropic") == 0);
}

static bool should_use_anthropic_messages(const char *model,
                                          const char *base_url,
                                          const char *api_mode)
{
    (void)model;
    (void)base_url;

    if (api_mode_is_anthropic_messages(api_mode)) {
        return true;
    }
    return false;
}

/* 规范化 OpenAI base URL，构造完整 chat/completions 路径 */
static void build_openai_api_url(void)
{
    s_openai_api_url[0] = '\0';
    if (!s_openai_base_url[0]) {
        return;
    }

    const char *base = s_openai_base_url;
    if (strstr(base, "/chat/completions")) {
        daima_safe_copy(s_openai_api_url, sizeof(s_openai_api_url), base);
        return;
    }

    if (s_use_anthropic_api) {
        if (strstr(base, "/v1/messages")) {
            daima_safe_copy(s_openai_api_url, sizeof(s_openai_api_url), base);
        } else if (daima_str_ends_with(base, "/")) {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%sv1/messages", base);
        } else {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/v1/messages", base);
        }
        return;
    }

    if (base_url_is_deepseek_official(base)) {
        if (daima_str_ends_with(base, "/")) {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%schat/completions", base);
        } else {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/chat/completions", base);
        }
        return;
    }

    if (strstr(base, "/v1/") || daima_str_ends_with(base, "/v1") || url_tail_is_version_root(base)) {
        if (daima_str_ends_with(base, "/")) {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%schat/completions", base);
        } else {
            snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/chat/completions", base);
        }
        return;
    }

    if (daima_str_ends_with(base, "/")) {
        snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%sv1/chat/completions", base);
    } else {
        snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/v1/chat/completions", base);
    }
}

/* 兼容部分 OpenAI 模型的 thinking/推理开关 */
static bool should_disable_thinking(void)
{
    const char *mode = runtime_config_get_provider_thinking_mode();
    if (mode && mode[0]) {
        if (strcasecmp(mode, "off") == 0 || strcasecmp(mode, "disabled") == 0) {
            return true;
        }
        if (strcasecmp(mode, "omit") == 0 || strcasecmp(mode, "auto") == 0) {
            return false;
        }
    }
    return false;
}

static const char *reasoning_effort_for_request(void)
{
    const char *mode = runtime_config_get_provider_thinking_mode();
    const char *effort = runtime_config_get_provider_reasoning_effort();
    if (mode && mode[0] &&
        (strcasecmp(mode, "off") == 0 || strcasecmp(mode, "disabled") == 0 ||
         strcasecmp(mode, "omit") == 0)) {
        return NULL;
    }
    if (effort && effort[0]) {
        return effort;
    }
    if (mode && mode[0]) {
        if (strcasecmp(mode, "low") == 0 || strcasecmp(mode, "medium") == 0 || strcasecmp(mode, "high") == 0) {
            return mode;
        }
        if (strcasecmp(mode, "on") == 0 || strcasecmp(mode, "enabled") == 0 || strcasecmp(mode, "auto") == 0) {
            return "medium";
        }
    }
    return NULL;
}

/* 是否需要额外的 reasoning_content 字段（兼容部分 API） */
static bool should_add_reasoning_content(void)
{
    return runtime_config_provider_needs_reasoning_content();
}

static bool should_use_max_tokens_field(void)
{
    return base_url_is_deepseek_official(s_openai_base_url);
}

static const char *llm_api_url(void)
{
    if (s_openai_api_url[0]) {
        return s_openai_api_url;
    }
    return DAIMA_OPENAI_API_URL;
}

static char *build_request_body(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                const char *model_name)
{
    int max_output_tokens = runtime_config_get_max_output_tokens();
    const char *request_model = (model_name && model_name[0]) ? model_name : s_model;
    cJSON *body = s_use_anthropic_api
        ? llm_anthropic_build_tools_body(
            system_prompt,
            messages,
            tools_json,
            request_model,
            max_output_tokens,
            should_disable_thinking(),
            reasoning_effort_for_request())
        : llm_openai_build_tools_body(
            system_prompt,
            messages,
            tools_json,
            request_model,
            max_output_tokens,
            should_use_max_tokens_field(),
            should_disable_thinking(),
            reasoning_effort_for_request(),
            should_add_reasoning_content());
    if (!body) {
        return NULL;
    }

    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    return post_data;
}

static struct curl_slist *build_headers(const char *url)
{
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (s_api_key[0] && url && strstr(url, "/anthropic/")) {
        char key_header[LLM_AUTH_HEADER_MAX];
        snprintf(key_header, sizeof(key_header), "x-api-key: %s", s_api_key);
        headers = curl_slist_append(headers, key_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else if (s_api_key[0]) {
        char auth[LLM_AUTH_HEADER_MAX];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", s_api_key);
        headers = curl_slist_append(headers, auth);
    }

    return headers;
}

static daima_err_t parse_llm_response(const char *raw_resp,
                                      long status,
                                      bool use_anthropic_api,
                                      llm_response_t *resp)
{
    if (!resp) {
        return DAIMA_ERR_INVALID_ARG;
    }
    memset(resp, 0, sizeof(*resp));

    if (status != 200) {
        DAIMA_LOGE(TAG, "API error %ld: %.500s", status, raw_resp ? raw_resp : "");
        return DAIMA_FAIL;
    }

    cJSON *diag_root = cJSON_Parse(raw_resp);
    if (diag_root) {
        log_llm_response_diagnostics(
            use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
            raw_resp,
            diag_root);
        cJSON_Delete(diag_root);
    } else {
        DAIMA_LOGW(TAG, "LLM diagnostics skipped: raw response JSON parse failed");
    }

    daima_err_t err = use_anthropic_api
        ? llm_anthropic_parse_response(raw_resp, resp)
        : llm_openai_parse_response(raw_resp, resp);
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Failed to parse API response JSON");
        return err;
    }

    DAIMA_LOGI(TAG, "Response: %d bytes text, %d tool calls, stop=%s",
             (int)resp->text_len, resp->call_count,
             resp->tool_use ? "tool_use" : "end_turn");
    if (resp->text && resp->text[0]) {
        DAIMA_LOGI(TAG, "LLM text: %zu bytes", strlen(resp->text));
    }

    return DAIMA_OK;
}

daima_err_t llm_proxy_init(void)
{
    const char *api_key = runtime_config_get_provider_api_key();
    const char *model = runtime_config_get_provider_model();
    const char *openai_base = runtime_config_get_provider_openai_base_url();
    const char *api_mode = runtime_config_get_provider_api_mode();

    if (api_key) {
        daima_safe_copy(s_api_key, sizeof(s_api_key), api_key);
        s_api_key_set = true;
    }

    if (model && model[0]) {
        daima_safe_copy(s_model, sizeof(s_model), model);
        s_model_set = true;
    }

    s_openai_base_url[0] = '\0';
    s_openai_api_url[0] = '\0';
    s_use_anthropic_api = false;
    if (openai_base && openai_base[0]) {
        daima_safe_copy(s_openai_base_url, sizeof(s_openai_base_url), openai_base);
        s_use_anthropic_api = should_use_anthropic_messages(s_model, s_openai_base_url, api_mode);
        build_openai_api_url();
    }

    if (s_model[0] == '\0' || strncmp(s_model, "claude", 6) == 0) {
        daima_safe_copy(s_model, sizeof(s_model), DEFAULT_LLM_MODEL);
    }

    if (s_api_key[0]) {
        DAIMA_LOGI(TAG, "LLM proxy initialized (protocol: %s, api_mode: %s, model: %s)",
                  s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
                  (api_mode && api_mode[0]) ? api_mode : "chat_completions(default)",
                  s_model);
    } else {
        DAIMA_LOGW(TAG, "No API key configured in %s", daima_path_runtime_config_file());
    }

    if (s_openai_base_url[0]) {
        DAIMA_LOGI(TAG, "LLM base URL: %s", s_openai_base_url);
        if (s_openai_api_url[0]) {
            DAIMA_LOGI(TAG, "LLM API URL: %s", s_openai_api_url);
        }
    }
    return DAIMA_OK;
}

/* 释放响应中堆内存字段 */
void llm_response_free(llm_response_t *resp)
{
    if (!resp) {
        return;
    }
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    free(resp->reasoning_content);
    resp->reasoning_content = NULL;
    resp->reasoning_content_len = 0;
    int call_count = resp->call_count;
    if (call_count < 0) {
        call_count = 0;
    }
    if (call_count > DAIMA_MAX_TOOL_CALLS) {
        call_count = DAIMA_MAX_TOOL_CALLS;
    }
    for (int i = 0; i < call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
        resp->calls[i].input_len = 0;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

/* 发送带工具的对话请求（非流式）并解析响应 */
daima_err_t llm_chat_tools(const char *system_prompt,
                          cJSON *messages,
                          const char *tools_json,
                          llm_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    if (s_api_key[0] == '\0') return DAIMA_ERR_INVALID_STATE;
    int max_output_tokens = runtime_config_get_max_output_tokens();
    int request_timeout_ms = runtime_config_get_request_timeout_ms();

    char *post_data = build_request_body(system_prompt, messages, tools_json, s_model);
    if (!post_data) return DAIMA_ERR_NO_MEM;

    DAIMA_LOGI(TAG, "Calling LLM API with tools (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)",
             s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
             s_model,
             max_output_tokens,
             request_timeout_ms,
             (int)strlen(post_data));
    llm_http_log_payload(TAG, "LLM tools request", post_data);

    char *raw_resp = NULL;
    int status = 0;
    daima_err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, request_timeout_ms, &raw_resp, &status);
    free(post_data);

    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "HTTP request failed: %s timeout_ms=%d", daima_err_to_name(err), request_timeout_ms);
        llm_http_log_payload(TAG, "LLM tools partial response", raw_resp);
        free(raw_resp);
        return err;
    }

    llm_http_log_payload(TAG, "LLM tools raw response", raw_resp);

    err = parse_llm_response(raw_resp, status, s_use_anthropic_api, resp);
    free(raw_resp);
    if (err != DAIMA_OK) {
        return err;
    }

    return DAIMA_OK;
}

daima_err_t llm_chat_tools_with_model(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model_override,
                                      llm_response_t *resp)
{
    if (!model_override || !model_override[0]) {
        return llm_chat_tools(system_prompt, messages, tools_json, resp);
    }

    char previous_model[LLM_MODEL_MAX_LEN];
    daima_safe_copy(previous_model, sizeof(previous_model), s_model);
    daima_safe_copy(s_model, sizeof(s_model), model_override);
    daima_err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
    daima_safe_copy(s_model, sizeof(s_model), previous_model);
    return err;
}

llm_async_chat_t *llm_chat_tools_async(const char *system_prompt,
                                       cJSON *messages,
                                       const char *tools_json,
                                       const char *model_override)
{
    if (s_api_key[0] == '\0') {
        return NULL;
    }

    llm_async_chat_t *chat = calloc(1, sizeof(*chat));
    if (!chat) {
        return NULL;
    }

    daima_safe_copy(chat->system_prompt_buf, sizeof(chat->system_prompt_buf), system_prompt ? system_prompt : "");
    daima_safe_copy(chat->tools_json_buf, sizeof(chat->tools_json_buf), tools_json ? tools_json : "");
    daima_safe_copy(chat->model_name, sizeof(chat->model_name),
                    (model_override && model_override[0]) ? model_override : s_model);
    chat->messages_ref = messages;
    chat->use_anthropic_api = s_use_anthropic_api;

    int max_output_tokens = runtime_config_get_max_output_tokens();
    int request_timeout_ms = runtime_config_get_request_timeout_ms();
    const char *request_tools = chat->tools_json_buf[0] ? chat->tools_json_buf : NULL;
    chat->post_data = build_request_body(chat->system_prompt_buf,
                                         chat->messages_ref,
                                         request_tools,
                                         chat->model_name);
    if (!chat->post_data) {
        free(chat);
        return NULL;
    }

    DAIMA_LOGI(TAG, "Launching async LLM API call (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)",
             chat->use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
             chat->model_name,
             max_output_tokens,
             request_timeout_ms,
             (int)strlen(chat->post_data));
    llm_http_log_payload(TAG, "LLM async tools request", chat->post_data);

    chat->headers = build_headers(llm_api_url());
    chat->http_req = llm_http_async_request("POST", llm_api_url(), chat->headers, chat->post_data, request_timeout_ms);
    if (!chat->http_req) {
        if (chat->headers) {
            curl_slist_free_all(chat->headers);
        }
        free(chat->post_data);
        free(chat);
        return NULL;
    }

    chat->launched = true;
    return chat;
}

bool llm_chat_async_is_done(llm_async_chat_t *chat)
{
    if (!chat || !chat->launched || !chat->http_req) {
        return true;
    }
    return llm_http_async_is_done(chat->http_req);
}

daima_err_t llm_chat_async_get_response(llm_async_chat_t *chat, llm_response_t *resp)
{
    if (!chat || !chat->launched || !chat->http_req || !resp) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char *raw_resp = NULL;
    long status = 0;
    daima_err_t err = llm_http_async_get_response(chat->http_req, &raw_resp, &status);
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Async HTTP request failed: %s", daima_err_to_name(err));
        llm_http_log_payload(TAG, "LLM async tools partial response", raw_resp);
        free(raw_resp);
        return err;
    }

    llm_http_log_payload(TAG, "LLM async tools raw response", raw_resp);
    err = parse_llm_response(raw_resp, status, chat->use_anthropic_api, resp);
    free(raw_resp);
    return err;
}

void llm_chat_async_free(llm_async_chat_t *chat)
{
    if (!chat) {
        return;
    }
    if (chat->http_req) {
        llm_http_async_free(chat->http_req);
    }
    if (chat->headers) {
        curl_slist_free_all(chat->headers);
    }
    free(chat->post_data);
    free(chat);
}

daima_err_t llm_set_api_key(const char *api_key)
{
    /* 覆盖当前进程内的运行时配置 */
    daima_safe_copy(s_api_key, sizeof(s_api_key), api_key);
    s_api_key_set = true;
    DAIMA_LOGI(TAG, "API key set");
    return DAIMA_OK;
}

daima_err_t llm_set_model(const char *model)
{
    /* 覆盖当前进程内的模型配置 */
    daima_safe_copy(s_model, sizeof(s_model), model);
    s_model_set = true;
    DAIMA_LOGI(TAG, "Model set to: %s", s_model);
    return DAIMA_OK;
}

const char *llm_get_model_name(void)
{
    return s_model[0] ? s_model : DEFAULT_LLM_MODEL;
}

int llm_get_context_limit_tokens(void)
{
    int configured = runtime_config_get_context_limit_tokens();
    if (configured > 0) {
        return configured;
    }
    return DEFAULT_CONTEXT_LIMIT_TOKENS;
}

#ifdef DAIMA_ENABLE_VISION

#include <stdio.h>

/* 根据扩展名推断 MIME 类型 */
static const char *get_mime_type_from_extension(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "image/png";
    
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    
    return "image/png";
}

daima_err_t llm_image_read_file(const char *image_path, llm_image_content_t *out_content)
{
    /* 读取图片文件并转为 base64 */
    if (!image_path || !out_content) return DAIMA_ERR_INVALID_ARG;
    
    memset(out_content, 0, sizeof(*out_content));

    FILE *fp = fopen(image_path, "rb");
    if (!fp) {
        DAIMA_LOGE(TAG, "Failed to open image file: %s", image_path);
        return DAIMA_ERR_NOT_FOUND;
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    /* 通过配置限制图片大小，避免占用过多内存 */
    const long max_size = (long)DAIMA_VISION_MAX_IMAGE_SIZE;
    if (file_size <= 0 || file_size > max_size) {
        DAIMA_LOGE(TAG, "Invalid image file size: %ld (max %ld)", file_size, max_size);
        fclose(fp);
        return DAIMA_ERR_INVALID_ARG;
    }
    
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        fclose(fp);
        return DAIMA_ERR_NO_MEM;
    }
    
    if (fread(file_data, 1, file_size, fp) != (size_t)file_size) {
        DAIMA_LOGE(TAG, "Failed to read image file");
        free(file_data);
        fclose(fp);
        return DAIMA_FAIL;
    }
    fclose(fp);
    
    size_t base64_len;
    char *base64_data = daima_base64_encode_alloc(file_data, file_size, &base64_len);
    free(file_data);
    
    if (!base64_data) {
        return DAIMA_ERR_NO_MEM;
    }
    
    out_content->image_data = base64_data;
    out_content->image_data_len = base64_len;
    daima_safe_copy(out_content->mime_type, sizeof(out_content->mime_type), get_mime_type_from_extension(image_path));
    
    return DAIMA_OK;
}

void llm_image_content_free(llm_image_content_t *content)
{
    if (content && content->image_data) {
        free(content->image_data);
        content->image_data = NULL;
        content->image_data_len = 0;
    }
}

/* 发送带图片的对话请求并解析文本响应（OpenAI 兼容） */
daima_err_t llm_chat_with_images(const char *system_prompt,
                                const char *user_text,
                                const llm_image_content_t *images,
                                int image_count,
                                llm_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    
    if (s_api_key[0] == '\0') return DAIMA_ERR_INVALID_STATE;
    if (!images || image_count <= 0) return DAIMA_ERR_INVALID_ARG;
    int max_output_tokens = runtime_config_get_max_output_tokens();
    int request_timeout_ms = runtime_config_get_request_timeout_ms();
    
    cJSON *body = s_use_anthropic_api
        ? llm_anthropic_build_image_body(
            system_prompt,
            user_text,
            images,
            image_count,
            s_model,
            max_output_tokens,
            should_disable_thinking(),
            reasoning_effort_for_request())
        : llm_openai_build_image_body(
            system_prompt,
            user_text,
            images,
            image_count,
            s_model,
            max_output_tokens,
            should_use_max_tokens_field(),
            should_disable_thinking(),
            reasoning_effort_for_request());
    if (!body) {
        return DAIMA_ERR_NO_MEM;
    }
    
    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    if (!post_data) return DAIMA_ERR_NO_MEM;
    
    DAIMA_LOGI(TAG, "Calling LLM API with images (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, images: %d)",
             s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
             s_model,
             max_output_tokens,
             request_timeout_ms,
             image_count);
    llm_http_log_payload(TAG, "LLM vision request", post_data);
    
    char *raw_resp = NULL;
    int status = 0;
    daima_err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, request_timeout_ms, &raw_resp, &status);
    free(post_data);
    
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "HTTP request failed: %s timeout_ms=%d", daima_err_to_name(err), request_timeout_ms);
        llm_http_log_payload(TAG, "LLM vision partial response", raw_resp);
        free(raw_resp);
        return err;
    }
    
    llm_http_log_payload(TAG, "LLM vision raw response", raw_resp);
    
    if (status != 200) {
        DAIMA_LOGE(TAG, "API error %d: %.500s", status, raw_resp ? raw_resp : "");
        free(raw_resp);
        return DAIMA_FAIL;
    }
    
    err = s_use_anthropic_api
        ? llm_anthropic_parse_response(raw_resp, resp)
        : llm_openai_parse_response(raw_resp, resp);
    free(raw_resp);
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Failed to parse API response JSON");
        return err;
    }
    
    DAIMA_LOGI(TAG, "Vision response: %d bytes text", (int)resp->text_len);
    
    return DAIMA_OK;
}

#endif /* DAIMA_ENABLE_VISION */
