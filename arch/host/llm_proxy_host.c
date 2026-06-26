/* Host 平台 LLM 代理实现：协议路由、HTTP 发送、响应解析。
 *
 * 核心职责：
 * - 根据 config.json 的 api_mode 选择 OpenAI Chat Completions 或 Anthropic Messages 协议
 * - 自动检测 DeepSeek 平台并适配 URL 格式和字段名差异
 * - 管理 API Key / Model 的进程内缓存（支持运行时覆盖）
 * - 提供同步和异步两种 LLM 调用模式
 * - 处理 thinking/reasoning 开关逻辑（不同模型的自定义字段）
 * - 收集响应诊断信息（token 消耗、空工具输入检测并存储）
 *
 * 协议路由规则：
 * - api_mode 为 "anthropic_messages" 或 "anthropic" → 使用 Anthropic Messages API
 * - 其他情况下 → 使用 OpenAI Chat Completions API
 * - DeepSeek 平台特殊处理：max_tokens 字段、非 ASCII Unicode 转义
 */

#include "drivers/llm/llm_proxy.h"
#include "runtime.h"
#include "drivers/llm/llm_anthropic_payload.h"
#include "drivers/llm/llm_openai_payload.h"
#include "arch/host/llm_http_client_host.h"
#include "base64.h"
#include "text.h"
#include "paths.h"
#include "autoconf.h"
#include "http.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "linux/slab.h"
#include "linux/kernel.h"
#include "arch/host/portability.h"

#include <stdlib.h>
#include "linux/printk.h"
#include "cjson.h"
#include "json_helpers.h"

/* 默认模型名称（未配置时降级使用） */
static const char *DEFAULT_LLM_MODEL = "kimi-k2.5";
/* 默认上下文窗口 token 数 */
static const int DEFAULT_CONTEXT_LIMIT_TOKENS = 128000;

/* ── 外部函数声明（已拆分到 llm_proxy_routing.c / llm_proxy_request.c） ── */

/* llm_proxy_routing.c */
extern void build_openai_api_url(void);
extern const char *llm_api_url(void);
extern bool should_use_anthropic_messages(const char *model,
                                          const char *base_url,
                                          const char *api_mode);
extern void build_api_url_for(const char *base, bool use_anthropic_api, char *out, size_t out_size);
extern const char *reasoning_effort_for_request(void);
extern bool should_disable_thinking(void);
extern bool should_use_max_tokens_field(void);
extern bool should_use_max_tokens_field_for_base_url(const char *base_url);

/* llm_proxy_request.c */
extern char *build_request_body(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                const char *model_name,
                                bool response_format_json_object);
extern char *build_request_body_with_options(const char *system_prompt,
                                             cJSON *messages,
                                             const char *tools_json,
                                             const char *model_name,
                                             bool response_format_json_object,
                                             const llm_request_options_t *options);
extern struct curl_slist *build_headers(const char *url);
extern struct curl_slist *build_headers_with_api_key(const char *url, const char *api_key);
extern err_t parse_llm_response(const char *raw_resp,
                                long status,
                                bool use_anthropic_api,
                                llm_response_t *resp);

/* ── 运行时配置缓存（由 config.json 注入） ── */
#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
#define LLM_AUTH_HEADER_MAX 352
char s_api_key[LLM_API_KEY_MAX_LEN] = {0};               /* API Key 缓存 */
char s_model[LLM_MODEL_MAX_LEN] = "kimi-k2.5";           /* 当前模型名缓存 */
char s_openai_base_url[BUF_SMALL] = {0};                 /* 配置中的 Base URL */
char s_openai_api_url[BUF_MEDIUM] = {0};                 /* 拼接后的完整 API URL */
bool s_use_anthropic_api = false;                        /* 是否使用 Anthropic 协议 */
bool s_api_key_set = false;                              /* API Key 是否已设置 */
bool s_model_set = false;                                /* 模型是否已设置 */
unsigned s_llm_debug_seq = 0;                            /* 调试文件序号（避免覆盖） */

/**
 * 异步 LLM 调用内部结构。
 * 保存请求上下文，等待 HTTP 异步回调完成。
 */
struct llm_async_chat {
	llm_async_request_t *http_req;    /* 底层 HTTP 异步请求句柄 */
	cJSON *messages_ref;              /* 消息数组引用（只读，不拥有） */
	char tools_json_buf[4096];        /* 工具 JSON 副本（异步安全） */
	char system_prompt_buf[2048];     /* system prompt 副本（异步安全） */
	char model_name[64];              /* 模型名副本（异步安全） */
	char request_url[BUF_MEDIUM];     /* 请求 URL 快照 */
	char request_api_key[LLM_API_KEY_MAX_LEN];
	char *post_data;                  /* JSON 请求体副本（异步安全） */
	struct curl_slist *headers;       /* HTTP 头链表（异步安全） */
	bool launched;                    /* 是否已发起 */
	bool use_anthropic_api;           /* 协议快照 */
};

/**
 * 初始化 LLM 代理。
 *
 * 从 config.json 读取：
 * - provider 的 api_key、model、openai_base_url、api_mode
 * - 根据 api_mode 选择协议（OpenAI / Anthropic）
 * - 拼接完整 API URL
 * - 模型名为空或以 "claude" 开头时回退为默认模型（防止意外使用不兼容模型名）
 *
 * @return  成功返回 0
 */
err_t llm_proxy_init(void)
{
	const char *api_key = runtime_config_get_provider_api_key();
	const char *model = runtime_config_get_provider_model();
	const char *openai_base = runtime_config_get_provider_openai_base_url();
	const char *api_mode = runtime_config_get_provider_api_mode();

	if (api_key) {
		safe_copy(s_api_key, sizeof(s_api_key), api_key);
		s_api_key_set = true;
	}

	if (model && model[0]) {
		safe_copy(s_model, sizeof(s_model), model);
		s_model_set = true;
	}

	s_openai_base_url[0] = '\0';
	s_openai_api_url[0] = '\0';
	s_use_anthropic_api = false;
	if (openai_base && openai_base[0]) {
		safe_copy(s_openai_base_url, sizeof(s_openai_base_url), openai_base);
		s_use_anthropic_api = should_use_anthropic_messages(s_model, s_openai_base_url, api_mode);
		build_openai_api_url();
	}

	/* 模型名以 claude 开头但未配置 Anthropic 协议时，回退为默认模型 */
	if (s_model[0] == '\0' || strncmp(s_model, "claude", 6) == 0) {
		safe_copy(s_model, sizeof(s_model), DEFAULT_LLM_MODEL);
	}

	if (s_api_key[0]) {
		pr_info("LLM proxy initialized (protocol: %s, api_mode: %s, model: %s)", s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible", (api_mode && api_mode[0]) ? api_mode : "chat_completions(default)", s_model);
	} else {
		pr_warn("No API key configured in %s/config.json", path_config_dir());
	}

	if (s_openai_base_url[0]) {
		pr_info("LLM base URL: %s", s_openai_base_url);
		if (s_openai_api_url[0]) {
			pr_info("LLM API URL: %s", s_openai_api_url);
		}
	}
	return 0;
}

/**
 * 释放 llm_response_t 内部的堆内存。
 * 释放 text、reasoning_content 以及所有 tool_call 的 input。
 *
 * @param resp  响应结构体（释放后字段清零）
 */
void llm_response_free(llm_response_t *resp)
{
	if (!resp) {
		return;
	}
	kfree(resp->text);
	resp->text = NULL;
	resp->text_len = 0;
	kfree(resp->reasoning_content);
	resp->reasoning_content = NULL;
	resp->reasoning_content_len = 0;
	int call_count = resp->call_count;
	if (call_count < 0) {
		call_count = 0;
	}
	if (call_count > MAX_TOOL_CALLS) {
		call_count = MAX_TOOL_CALLS;
	}
	for (int i = 0; i < call_count; i++) {
		kfree(resp->calls[i].input);
		resp->calls[i].input = NULL;
		resp->calls[i].input_len = 0;
	}
	resp->call_count = 0;
	resp->tool_use = false;
}

/**
 * 同步发送带工具调用的 LLM 请求。
 *
 * 执行流程：
 * 1. 构建请求体 → 2. HTTP POST → 3. 解析响应
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组
 * @param tools_json     工具定义 JSON
 * @param resp           输出响应
 * @return               成功返回 0
 */
err_t llm_chat_tools(const char *system_prompt,
                          cJSON *messages,
                          const char *tools_json,
                          llm_response_t *resp)
{
	memset(resp, 0, sizeof(*resp));

	if (s_api_key[0] == '\0') return ERR_INVALID_STATE;
	int max_output_tokens = runtime_config_get_max_output_tokens();
	int request_timeout_ms = runtime_config_get_request_timeout_ms();

	char *post_data = build_request_body(system_prompt, messages, tools_json, s_model, false);
	if (!post_data) return ERR_NO_MEM;

	pr_info("Calling LLM API with tools (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)", s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible", s_model, max_output_tokens, request_timeout_ms, (int)strlen(post_data));
	llm_http_log_payload("llm", "LLM tools request", post_data);

	char *raw_resp = NULL;
	int status = 0;
	err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, request_timeout_ms, &raw_resp, &status);
	kfree(post_data);

	if (err != 0) {
		pr_err("HTTP request failed: %s timeout_ms=%d", err_name(err), request_timeout_ms);
		llm_http_log_payload("llm", "LLM tools partial response", raw_resp);
		kfree(raw_resp);
		return err;
	}

	llm_http_log_payload("llm", "LLM tools raw response", raw_resp);

	err = parse_llm_response(raw_resp, status, s_use_anthropic_api, resp);
	kfree(raw_resp);
	if (err != 0) {
		return err;
	}

	return 0;
}

err_t llm_chat_tools_with_model_and_format(const char *system_prompt,
                                           cJSON *messages,
                                           const char *tools_json,
                                           const char *model_override,
                                           bool response_format_json_object,
                                           llm_response_t *resp)
{
	return llm_chat_tools_with_provider_and_format(system_prompt,
	                                               messages,
	                                               tools_json,
	                                               NULL,
	                                               model_override,
	                                               response_format_json_object,
	                                               resp);
}

err_t llm_chat_tools_with_provider_and_format(const char *system_prompt,
                                              cJSON *messages,
                                              const char *tools_json,
                                              const char *provider_name,
                                              const char *model_override,
                                              bool response_format_json_object,
                                              llm_response_t *resp)
{
	memset(resp, 0, sizeof(*resp));

	const char *request_api_key = provider_name && provider_name[0]
	    ? runtime_config_get_provider_api_key_for_name(provider_name)
	    : s_api_key;
	const char *request_base_url = provider_name && provider_name[0]
	    ? runtime_config_get_provider_openai_base_url_for_name(provider_name)
	    : runtime_config_get_provider_openai_base_url();
	const char *request_api_mode = provider_name && provider_name[0]
	    ? runtime_config_get_provider_api_mode_for_name(provider_name)
	    : runtime_config_get_provider_api_mode();
	const char *request_thinking_mode = provider_name && provider_name[0]
	    ? runtime_config_get_provider_thinking_mode_for_name(provider_name)
	    : runtime_config_get_provider_thinking_mode();
	const char *request_reasoning_effort = provider_name && provider_name[0]
	    ? runtime_config_get_provider_reasoning_effort_for_name(provider_name)
	    : runtime_config_get_provider_reasoning_effort();
	bool request_needs_reasoning_content = provider_name && provider_name[0]
	    ? runtime_config_provider_needs_reasoning_content_for_name(provider_name)
	    : runtime_config_provider_needs_reasoning_content();
	int max_output_tokens = provider_name && provider_name[0]
	    ? runtime_config_get_max_output_tokens_for_name(provider_name)
	    : runtime_config_get_max_output_tokens();
	int request_timeout_ms = provider_name && provider_name[0]
	    ? runtime_config_get_request_timeout_ms_for_name(provider_name)
	    : runtime_config_get_request_timeout_ms();
	const char *request_model = (model_override && model_override[0])
	    ? model_override
	    : ((provider_name && provider_name[0])
	           ? runtime_config_get_provider_model_for_name(provider_name)
	           : s_model);

	if (!request_api_key || !request_api_key[0]) return ERR_INVALID_STATE;
	if (!request_model || !request_model[0]) return ERR_INVALID_STATE;

	bool use_anthropic_api = should_use_anthropic_messages(request_model, request_base_url, request_api_mode);
	char request_url[BUF_MEDIUM];
	request_url[0] = '\0';
	if (request_base_url && request_base_url[0]) {
		build_api_url_for(request_base_url, use_anthropic_api, request_url, sizeof(request_url));
	} else {
		strscpy(request_url, llm_api_url(), sizeof(request_url));
	}

	llm_request_options_t options = {
	    .api_key = request_api_key,
	    .model = request_model,
	    .base_url = request_base_url,
	    .api_url = request_url,
	    .api_mode = request_api_mode,
	    .thinking_mode = request_thinking_mode,
	    .reasoning_effort = request_reasoning_effort,
	    .max_output_tokens = max_output_tokens,
	    .request_timeout_ms = request_timeout_ms,
	    .use_anthropic_api = use_anthropic_api,
	    .add_reasoning_content = request_needs_reasoning_content,
	    .use_max_tokens_field = should_use_max_tokens_field_for_base_url(request_base_url),
	};

	char *post_data = build_request_body_with_options(system_prompt,
	                                                  messages,
	                                                  tools_json,
	                                                  request_model,
	                                                  response_format_json_object,
	                                                  &options);
	if (!post_data) return ERR_NO_MEM;

	pr_info("Calling LLM API with tools (protocol: %s, provider: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)",
	        use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
	        provider_name && provider_name[0] ? provider_name : runtime_config_get_active_provider_name(),
	        request_model,
	        max_output_tokens,
	        request_timeout_ms,
	        (int)strlen(post_data));
	llm_http_log_payload("llm", "LLM tools request", post_data);

	char *raw_resp = NULL;
	int status = 0;
	err_t err = llm_http_post_json(request_url, request_api_key, post_data, request_timeout_ms, &raw_resp, &status);
	kfree(post_data);

	if (err != 0) {
		pr_err("HTTP request failed: %s timeout_ms=%d provider=%s", err_name(err), request_timeout_ms,
		       provider_name && provider_name[0] ? provider_name : runtime_config_get_active_provider_name());
		llm_http_log_payload("llm", "LLM tools partial response", raw_resp);
		kfree(raw_resp);
		return err;
	}

	llm_http_log_payload("llm", "LLM tools raw response", raw_resp);
	err = parse_llm_response(raw_resp, status, use_anthropic_api, resp);
	kfree(raw_resp);
	return err;
}

/**
 * 带模型覆盖的工具调用。
 *
 * 临时切换模型发起请求，完成后恢复原有模型。
 * 用于分类路由等需要指定特定模型进行推断的场景。
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组
 * @param tools_json     工具定义 JSON
 * @param model_override 临时模型名称；为 NULL 时退回到 llm_chat_tools
 * @param resp           输出响应
 * @return               成功返回 0
 */
err_t llm_chat_tools_with_model(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model_override,
                                      llm_response_t *resp)
{
	return llm_chat_tools_with_model_and_format(system_prompt,
	                                            messages,
	                                            tools_json,
	                                            model_override,
	                                            false,
	                                            resp);
}

/**
 * 发起异步 LLM 工具调用。
 *
 * 与 llm_chat_tools 参数相同，但不阻塞等待响应。
 * 内部将请求参数深拷贝到 llm_async_chat_t 结构体中（异步安全）。
 * 通过 llm_chat_async_is_done 轮询完成状态，
 * 通过 llm_chat_async_get_response 获取结果。
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组（引用计数，调用者需保持有效直到释放异步句柄）
 * @param tools_json     工具定义 JSON
 * @param model_override 模型名称；NULL 使用当前模型
 * @return               异步调用句柄；失败返回 NULL
 */
llm_async_chat_t *llm_chat_tools_async(const char *system_prompt,
                                       cJSON *messages,
                                       const char *tools_json,
                                       const char *model_override)
{
	if (s_api_key[0] == '\0') {
		return NULL;
	}

	llm_async_chat_t *chat = kzalloc(sizeof(*chat), GFP_KERNEL);
	if (!chat) {
		return NULL;
	}

	/* 深拷贝所有字符串参数（异步安全：原始参数可能在返回后被释放） */
	safe_copy(chat->system_prompt_buf, sizeof(chat->system_prompt_buf), system_prompt ? system_prompt : "");
	safe_copy(chat->tools_json_buf, sizeof(chat->tools_json_buf), tools_json ? tools_json : "");
	safe_copy(chat->model_name, sizeof(chat->model_name),
	                (model_override && model_override[0]) ? model_override : s_model);
	chat->messages_ref = messages;  /* 引用传递，调用者需保持有效 */
	chat->use_anthropic_api = s_use_anthropic_api;  /* 协议快照 */

	int max_output_tokens = runtime_config_get_max_output_tokens();
	int request_timeout_ms = runtime_config_get_request_timeout_ms();
	const char *request_tools = chat->tools_json_buf[0] ? chat->tools_json_buf : NULL;
	chat->post_data = build_request_body(chat->system_prompt_buf,
	                                     chat->messages_ref,
	                                     request_tools,
	                                     chat->model_name,
	                                     false);
	if (!chat->post_data) {
		kfree(chat);
		return NULL;
	}

	pr_info("Launching async LLM API call (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)", chat->use_anthropic_api ? "anthropic-compatible" : "openai-compatible", chat->model_name, max_output_tokens, request_timeout_ms, (int)strlen(chat->post_data));
	llm_http_log_payload("llm", "LLM async tools request", chat->post_data);

	chat->headers = build_headers(llm_api_url());
	chat->http_req = llm_http_async_request("POST", llm_api_url(), chat->headers, chat->post_data, request_timeout_ms);
	if (!chat->http_req) {
		if (chat->headers) {
			curl_slist_free_all(chat->headers);
		}
		kfree(chat->post_data);
		kfree(chat);
		return NULL;
	}

	chat->launched = true;
	return chat;
}

llm_async_chat_t *llm_chat_tools_async_with_format(const char *system_prompt,
                                                   cJSON *messages,
                                                   const char *tools_json,
                                                   const char *model_override,
                                                   bool response_format_json_object)
{
	return llm_chat_tools_async_with_provider_and_format(system_prompt,
	                                                     messages,
	                                                     tools_json,
	                                                     NULL,
	                                                     model_override,
	                                                     response_format_json_object);
}

llm_async_chat_t *llm_chat_tools_async_with_provider_and_format(const char *system_prompt,
                                                                cJSON *messages,
                                                                const char *tools_json,
                                                                const char *provider_name,
                                                                const char *model_override,
                                                                bool response_format_json_object)
{
	if (!response_format_json_object) {
		if (!provider_name || !provider_name[0]) {
			return llm_chat_tools_async(system_prompt, messages, tools_json, model_override);
		}
	}

	const char *request_api_key = provider_name && provider_name[0]
	    ? runtime_config_get_provider_api_key_for_name(provider_name)
	    : s_api_key;
	const char *request_base_url = provider_name && provider_name[0]
	    ? runtime_config_get_provider_openai_base_url_for_name(provider_name)
	    : runtime_config_get_provider_openai_base_url();
	const char *request_api_mode = provider_name && provider_name[0]
	    ? runtime_config_get_provider_api_mode_for_name(provider_name)
	    : runtime_config_get_provider_api_mode();
	const char *request_thinking_mode = provider_name && provider_name[0]
	    ? runtime_config_get_provider_thinking_mode_for_name(provider_name)
	    : runtime_config_get_provider_thinking_mode();
	const char *request_reasoning_effort = provider_name && provider_name[0]
	    ? runtime_config_get_provider_reasoning_effort_for_name(provider_name)
	    : runtime_config_get_provider_reasoning_effort();
	bool request_needs_reasoning_content = provider_name && provider_name[0]
	    ? runtime_config_provider_needs_reasoning_content_for_name(provider_name)
	    : runtime_config_provider_needs_reasoning_content();
	int max_output_tokens = provider_name && provider_name[0]
	    ? runtime_config_get_max_output_tokens_for_name(provider_name)
	    : runtime_config_get_max_output_tokens();
	int request_timeout_ms = provider_name && provider_name[0]
	    ? runtime_config_get_request_timeout_ms_for_name(provider_name)
	    : runtime_config_get_request_timeout_ms();
	const char *request_model = (model_override && model_override[0])
	    ? model_override
	    : ((provider_name && provider_name[0])
	           ? runtime_config_get_provider_model_for_name(provider_name)
	           : s_model);

	if (!request_api_key || !request_api_key[0] || !request_model || !request_model[0]) {
		return NULL;
	}

	llm_async_chat_t *chat = kzalloc(sizeof(*chat), GFP_KERNEL);
	if (!chat) {
		return NULL;
	}

	safe_copy(chat->system_prompt_buf, sizeof(chat->system_prompt_buf), system_prompt ? system_prompt : "");
	safe_copy(chat->tools_json_buf, sizeof(chat->tools_json_buf), tools_json ? tools_json : "");
	safe_copy(chat->model_name, sizeof(chat->model_name), request_model);
	safe_copy(chat->request_api_key, sizeof(chat->request_api_key), request_api_key);
	chat->messages_ref = messages;
	chat->use_anthropic_api = should_use_anthropic_messages(request_model, request_base_url, request_api_mode);

	if (request_base_url && request_base_url[0]) {
		build_api_url_for(request_base_url, chat->use_anthropic_api, chat->request_url, sizeof(chat->request_url));
	} else {
		strscpy(chat->request_url, llm_api_url(), sizeof(chat->request_url));
	}

	const char *request_tools = chat->tools_json_buf[0] ? chat->tools_json_buf : NULL;
	llm_request_options_t options = {
	    .api_key = request_api_key,
	    .model = request_model,
	    .base_url = request_base_url,
	    .api_url = chat->request_url,
	    .api_mode = request_api_mode,
	    .thinking_mode = request_thinking_mode,
	    .reasoning_effort = request_reasoning_effort,
	    .max_output_tokens = max_output_tokens,
	    .request_timeout_ms = request_timeout_ms,
	    .use_anthropic_api = chat->use_anthropic_api,
	    .add_reasoning_content = request_needs_reasoning_content,
	    .use_max_tokens_field = should_use_max_tokens_field_for_base_url(request_base_url),
	};
	chat->post_data = build_request_body_with_options(chat->system_prompt_buf,
	                                                  chat->messages_ref,
	                                                  request_tools,
	                                                  chat->model_name,
	                                                  response_format_json_object,
	                                                  &options);
	if (!chat->post_data) {
		kfree(chat);
		return NULL;
	}

	pr_info("Launching async LLM API call (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, body: %d bytes)",
	        chat->use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
	        chat->model_name,
	        max_output_tokens,
	        request_timeout_ms,
	        (int)strlen(chat->post_data));
	llm_http_log_payload("llm", "LLM async tools request", chat->post_data);

	chat->headers = build_headers_with_api_key(chat->request_url, chat->request_api_key);
	chat->http_req = llm_http_async_request("POST", chat->request_url, chat->headers, chat->post_data, request_timeout_ms);
	if (!chat->http_req) {
		if (chat->headers) {
			curl_slist_free_all(chat->headers);
		}
		kfree(chat->post_data);
		kfree(chat);
		return NULL;
	}

	chat->launched = true;
	return chat;
}

/**
 * 非阻塞检查异步 LLM 调用是否完成。
 *
 * @param chat  异步调用句柄
 * @return      true 表示已完成
 */
bool llm_chat_async_is_done(llm_async_chat_t *chat)
{
	if (!chat || !chat->launched || !chat->http_req) {
		return true;
	}
	return llm_http_async_is_done(chat->http_req);
}

/**
 * 获取异步 LLM 调用的响应（阻塞直到完成）。
 *
 * @param chat  异步调用句柄
 * @param resp  输出响应
 * @return      成功返回 0
 */
err_t llm_chat_async_get_response(llm_async_chat_t *chat, llm_response_t *resp)
{
	if (!chat || !chat->launched || !chat->http_req || !resp) {
		return ERR_INVALID_ARG;
	}

	char *raw_resp = NULL;
	long status = 0;
	err_t err = llm_http_async_get_response(chat->http_req, &raw_resp, &status);
	if (err != 0) {
		pr_err("Async HTTP request failed: %s", err_name(err));
		llm_http_log_payload("llm", "LLM async tools partial response", raw_resp);
		kfree(raw_resp);
		return err;
	}

	llm_http_log_payload("llm", "LLM async tools raw response", raw_resp);
	err = parse_llm_response(raw_resp, status, chat->use_anthropic_api, resp);
	kfree(raw_resp);
	return err;
}

/**
 * 释放异步 LLM 调用及其资源。
 *
 * @param chat  异步调用句柄（可为 NULL）
 */
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
	kfree(chat->post_data);
	kfree(chat);
}

/**
 * 运行时覆盖 API Key（进程内）。
 *
 * @param api_key  新 API Key 字符串
 * @return         成功返回 0
 */
err_t llm_set_api_key(const char *api_key)
{
	safe_copy(s_api_key, sizeof(s_api_key), api_key);
	s_api_key_set = true;
	pr_info("API key set");
	return 0;
}

/**
 * 运行时覆盖模型名称（进程内）。
 *
 * @param model  新模型名称
 * @return       成功返回 0
 */
err_t llm_set_model(const char *model)
{
	safe_copy(s_model, sizeof(s_model), model);
	s_model_set = true;
	pr_info("Model set to: %s", s_model);
	return 0;
}

/**
 * 获取当前生效的模型名称。
 *
 * @return 模型名称（始终非空，未设置时返回默认值）
 */
const char *llm_get_model_name(void)
{
	return s_model[0] ? s_model : DEFAULT_LLM_MODEL;
}

/**
 * 获取当前模型的上下文窗口大小估计值。
 *
 * 优先使用 config.json 中的 context_limit_tokens，
 * 未配置时返回默认值 128000。
 *
 * @return token 数
 */
int llm_get_context_limit_tokens(void)
{
	int configured = runtime_config_get_context_limit_tokens();
	if (configured > 0) {
		return configured;
	}
	return DEFAULT_CONTEXT_LIMIT_TOKENS;
}

#ifdef ENABLE_VISION

#include <stdio.h>

/**
 * 根据文件扩展名推断 MIME 类型。
 * 支持 png / jpg / jpeg / webp / gif。
 *
 * @param path  文件路径
 * @return      MIME 类型字符串
 */
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

/**
 * 读取图片文件并转为 base64 编码。
 *
 * 流程：
 * 1. 打开文件并读取二进制数据
 * 2. 检查文件大小（受 VISION_MAX_IMAGE_SIZE 限制）
 * 3. base64 编码
 * 4. 根据扩展名推断 MIME 类型
 *
 * @param image_path   图片文件路径
 * @param out_content  输出 base64 编码的图片内容（调用者通过 llm_image_content_free 释放）
 * @return             成功返回 0
 */
err_t llm_image_read_file(const char *image_path, llm_image_content_t *out_content)
{
	if (!image_path || !out_content) return ERR_INVALID_ARG;
	
	memset(out_content, 0, sizeof(*out_content));

	FILE *fp = fopen(image_path, "rb");
	if (!fp) {
		pr_err("Failed to open image file: %s", image_path);
		return ERR_NOT_FOUND;
	}
	
	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	/* 配置级大小限制，避免占用过多内存 */
	const long max_size = (long)VISION_MAX_IMAGE_SIZE;
	if (file_size <= 0 || file_size > max_size) {
		pr_err("Invalid image file size: %ld (max %ld)", file_size, max_size);
		fclose(fp);
		return ERR_INVALID_ARG;
	}
	
	unsigned char *file_data = kmalloc(file_size, GFP_KERNEL);
	if (!file_data) {
		fclose(fp);
		return ERR_NO_MEM;
	}
	
	if (fread(file_data, 1, file_size, fp) != (size_t)file_size) {
		pr_err("Failed to read image file");
		kfree(file_data);
		fclose(fp);
		return ERR_FAIL;
	}
	fclose(fp);
	
	size_t base64_len;
	char *base64_data = base64_encode_alloc(file_data, file_size, &base64_len);
	kfree(file_data);
	
	if (!base64_data) {
		return ERR_NO_MEM;
	}
	
	out_content->image_data = base64_data;
	out_content->image_data_len = base64_len;
	safe_copy(out_content->mime_type, sizeof(out_content->mime_type), get_mime_type_from_extension(image_path));
	
	return 0;
}

/**
 * 释放图片内容块的堆内存。
 *
 * @param content  图片内容块（释放后字段清零）
 */
void llm_image_content_free(llm_image_content_t *content)
{
	if (content && content->image_data) {
		kfree(content->image_data);
		content->image_data = NULL;
		content->image_data_len = 0;
	}
}

/**
 * 发送带图片的视觉理解请求。
 *
 * 根据当前协议选择对应的 image_body 构造函数（OpenAI / Anthropic）。
 * 响应仅提取文本内容（不支持工具调用的视觉场景）。
 *
 * @param system_prompt  系统提示词
 * @param user_text      用户文本
 * @param images         图片数组
 * @param image_count    图片数量
 * @param resp           输出响应
 * @return               成功返回 0
 */
err_t llm_chat_with_images(const char *system_prompt,
                                const char *user_text,
                                const llm_image_content_t *images,
                                int image_count,
                                llm_response_t *resp)
{
	memset(resp, 0, sizeof(*resp));
	
	if (s_api_key[0] == '\0') return ERR_INVALID_STATE;
	if (!images || image_count <= 0) return ERR_INVALID_ARG;
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
		return ERR_NO_MEM;
	}
	
	char *post_data = cJSON_PrintUnformatted(body);
	cJSON_Delete(body);
	
	if (!post_data) return ERR_NO_MEM;
	
	pr_info("Calling LLM API with images (protocol: %s, model: %s, max_output_tokens: %d, timeout_ms: %d, images: %d)", s_use_anthropic_api ? "anthropic-compatible" : "openai-compatible", s_model, max_output_tokens, request_timeout_ms, image_count);
	llm_http_log_payload("llm", "LLM vision request", post_data);
	
	char *raw_resp = NULL;
	int status = 0;
	err_t err = llm_http_post_json(llm_api_url(), s_api_key, post_data, request_timeout_ms, &raw_resp, &status);
	kfree(post_data);
	
	if (err != 0) {
		pr_err("HTTP request failed: %s timeout_ms=%d", err_name(err), request_timeout_ms);
		llm_http_log_payload("llm", "LLM vision partial response", raw_resp);
		kfree(raw_resp);
		return err;
	}
	
	llm_http_log_payload("llm", "LLM vision raw response", raw_resp);
	
	if (status != 200) {
		pr_err("API error %d: %.500s", status, raw_resp ? raw_resp : "");
		kfree(raw_resp);
		return ERR_FAIL;
	}
	
	err = s_use_anthropic_api
		? llm_anthropic_parse_response(raw_resp, resp)
		: llm_openai_parse_response(raw_resp, resp);
	kfree(raw_resp);
	if (err != 0) {
		pr_err("Failed to parse API response JSON");
		return err;
	}
	
	pr_info("Vision response: %d bytes text", (int)resp->text_len);
	
	return 0;
}

#endif /* ENABLE_VISION */
