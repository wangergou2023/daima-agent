/* Host 侧 LLM 代理实现：基于 libcurl 的 HTTP 请求 */
#include "llm/llm_proxy.h"
#include "app/runtime_config.h"
#include "llm/llm_openai_payload.h"
#include "llm/llm_http_client_host.h"
#include "daima_base64.h"
#include "daima_text.h"
#include "app/daima_paths.h"
#include "daima_config.h"
#include "host_http.h"

#include <string.h>

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
#include "daima_log.h"
#include "cJSON.h"

static const char *TAG = "llm";
static const char *DEFAULT_LLM_MODEL = "kimi-k2.5";
static const int DEFAULT_CONTEXT_LIMIT_TOKENS = 128000;

/* 运行期配置缓存（由 config.json 注入） */
#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
static char s_api_key[LLM_API_KEY_MAX_LEN] = {0};
static char s_model[LLM_MODEL_MAX_LEN] = "kimi-k2.5";
static char s_openai_base_url[256] = {0};
static char s_openai_api_url[512] = {0};
static bool s_api_key_set = false;
static bool s_model_set = false;

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

/* 是否需要额外的 reasoning_content 字段（兼容部分 API） */
static bool should_add_reasoning_content(void)
{
    return runtime_config_provider_needs_reasoning_content();
}

static const char *llm_api_url(void)
{
    if (s_openai_api_url[0]) {
        return s_openai_api_url;
    }
    return DAIMA_OPENAI_API_URL;
}

daima_err_t llm_proxy_init(void)
{
    const char *api_key = runtime_config_get_provider_api_key();
    const char *model = runtime_config_get_provider_model();
    const char *openai_base = runtime_config_get_provider_openai_base_url();

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
    if (openai_base && openai_base[0]) {
        daima_safe_copy(s_openai_base_url, sizeof(s_openai_base_url), openai_base);
        build_openai_api_url();
    }

    if (s_model[0] == '\0' || strncmp(s_model, "claude", 6) == 0) {
        daima_safe_copy(s_model, sizeof(s_model), DEFAULT_LLM_MODEL);
    }

    if (s_api_key[0]) {
        DAIMA_LOGI(TAG, "LLM proxy initialized (protocol: openai-compatible, model: %s)", s_model);
    } else {
        DAIMA_LOGW(TAG, "No API key configured in %s", daima_path_runtime_config_file());
    }

    if (s_openai_base_url[0]) {
        DAIMA_LOGI(TAG, "OpenAI base URL: %s", s_openai_base_url);
        if (s_openai_api_url[0]) {
            DAIMA_LOGI(TAG, "OpenAI API URL: %s", s_openai_api_url);
        }
    }
    return DAIMA_OK;
}

/* 释放响应中堆内存字段 */
void llm_response_free(llm_response_t *resp)
{
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    free(resp->reasoning_content);
    resp->reasoning_content = NULL;
    resp->reasoning_content_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
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

    cJSON *body = llm_openai_build_tools_body(
        system_prompt,
        messages,
        tools_json,
        s_model,
        DAIMA_LLM_MAX_TOKENS,
        should_disable_thinking(),
        should_add_reasoning_content());
    if (!body) {
        return DAIMA_ERR_NO_MEM;
    }

    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) return DAIMA_ERR_NO_MEM;

    DAIMA_LOGI(TAG, "Calling LLM API with tools (protocol: openai-compatible, model: %s, body: %d bytes)",
             s_model, (int)strlen(post_data));
    llm_http_log_payload(TAG, "LLM tools request", post_data);

    char *raw_resp = NULL;
    int status = 0;
    daima_err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, 120 * 1000, &raw_resp, &status);
    free(post_data);

    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "HTTP request failed: %s", daima_err_to_name(err));
        llm_http_log_payload(TAG, "LLM tools partial response", raw_resp);
        free(raw_resp);
        return err;
    }

    llm_http_log_payload(TAG, "LLM tools raw response", raw_resp);

    if (status != 200) {
        DAIMA_LOGE(TAG, "API error %d: %.500s", status, raw_resp ? raw_resp : "");
        free(raw_resp);
        return DAIMA_FAIL;
    }

    err = llm_openai_parse_response(raw_resp, resp);
    free(raw_resp);
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
    
    cJSON *body = llm_openai_build_image_body(
        system_prompt,
        user_text,
        images,
        image_count,
        s_model,
        DAIMA_LLM_MAX_TOKENS,
        should_disable_thinking());
    if (!body) {
        return DAIMA_ERR_NO_MEM;
    }
    
    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    if (!post_data) return DAIMA_ERR_NO_MEM;
    
    DAIMA_LOGI(TAG, "Calling LLM API with images (protocol: openai-compatible, model: %s, images: %d)",
             s_model, image_count);
    llm_http_log_payload(TAG, "LLM vision request", post_data);
    
    char *raw_resp = NULL;
    int status = 0;
    daima_err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, 120 * 1000, &raw_resp, &status);
    free(post_data);
    
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "HTTP request failed: %s", daima_err_to_name(err));
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
    
    err = llm_openai_parse_response(raw_resp, resp);
    free(raw_resp);
    if (err != DAIMA_OK) {
        DAIMA_LOGE(TAG, "Failed to parse API response JSON");
        return err;
    }
    
    DAIMA_LOGI(TAG, "Vision response: %d bytes text", (int)resp->text_len);
    
    return DAIMA_OK;
}

#endif /* DAIMA_ENABLE_VISION */
