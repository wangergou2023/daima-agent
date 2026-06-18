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

/* ── 运行时配置缓存（由 config.json 注入） ── */
#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
#define LLM_AUTH_HEADER_MAX 352
static char s_api_key[LLM_API_KEY_MAX_LEN] = {0};        /* API Key 缓存 */
static char s_model[LLM_MODEL_MAX_LEN] = "kimi-k2.5";    /* 当前模型名缓存 */
static char s_openai_base_url[BUF_SMALL] = {0};           /* 配置中的 Base URL */
static char s_openai_api_url[BUF_MEDIUM] = {0};           /* 拼接后的完整 API URL */
static bool s_use_anthropic_api = false;                  /* 是否使用 Anthropic 协议 */
static bool s_api_key_set = false;                        /* API Key 是否已设置 */
static bool s_model_set = false;                          /* 模型是否已设置 */
static unsigned s_llm_debug_seq = 0;                      /* 调试文件序号（避免覆盖） */

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
	char *post_data;                  /* JSON 请求体副本（异步安全） */
	struct curl_slist *headers;       /* HTTP 头链表（异步安全） */
	bool launched;                    /* 是否已发起 */
	bool use_anthropic_api;           /* 协议快照 */
};

/**
 * 递归创建目录路径（用于调试文件输出）。
 */
static void ensure_dir_path(const char *path)
{
	if (!path || !path[0]) {
		return;
	}
	char tmp[BUF_MEDIUM];
	strscpy(tmp, path, sizeof(tmp));
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

/**
 * 检查 JSON 对象是否为空（无任何子项）。
 */
static bool object_is_empty(cJSON *obj)
{
	return cJSON_IsObject(obj) && obj->child == NULL;
}

/**
 * 记录 LLM 响应的详细诊断信息。
 *
 * 统计内容：
 * - 模型/请求 ID/停止原因
 * - token 消耗（input/output/total，兼容 prompt_tokens/completion_tokens 字段名差异）
 * - 内容块分类（text / thinking / tool_use）
 * - 空工具输入检测（空对象 "{}" 的 tool 输入）
 *
 * 对于空工具输入场景，将完整响应体保存到 cache/llm_debug/ 目录便于复盘。
 *
 * @param protocol  协议标识（"openai-compatible" 或 "anthropic-compatible"）
 * @param raw_resp  原始响应文本
 * @param root      解析后的 cJSON 根对象
 */
static void log_llm_response_diagnostics(const char *protocol,
                                         const char *raw_resp,
                                         cJSON *root)
{
	if (!root) {
		return;
	}

	/* 提取基础元信息：id / model / stop_reason（或 finish_reason） */
	const char *id = json_string(root, "id");
	const char *model = json_string(root, "model");
	const char *stop = json_string(root, "stop_reason");
	if (!stop) {
		stop = json_string(root, "finish_reason");
	}

	/* 提取 token 使用统计（兼容 OpenAI 和 Anthropic 的字段名差异） */
	cJSON *usage = cJSON_GetObjectItem(root, "usage");
	int input_tokens = usage ? json_number(usage, "input_tokens") : -1;
	int output_tokens = usage ? json_number(usage, "output_tokens") : -1;
	int total_tokens = usage ? json_number(usage, "total_tokens") : -1;
	/* OpenAI 响应中 token 字段名可能是 prompt_tokens/completion_tokens */
	if (input_tokens < 0 && usage) input_tokens = json_number(usage, "prompt_tokens");
	if (output_tokens < 0 && usage) output_tokens = json_number(usage, "completion_tokens");

	/* 统计 Anthropic 风格的 content 块 */
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
				/* 检测空输入：{} → 可能表示模型调用工具但未提供参数 */
				if (object_is_empty(input)) {
					empty_tool_inputs++;
					const char *name = json_string(block, "name");
					if (name && !empty_tool_name[0]) {
						strscpy(empty_tool_name, name, sizeof(empty_tool_name));
					}
				}
				kfree(input_json);
			}
		}
	}

	/* 统计 OpenAI 风格的 tool_calls（取 message.tool_calls 而非 content 块） */
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
							strscpy(empty_tool_name, name, sizeof(empty_tool_name));
						}
					}
				}
			}
		}
	}

	pr_info("LLM diagnostics: protocol=%s id=%s model=%s stop=%s usage_in=%d usage_out=%d usage_total=%d raw_bytes=%d blocks{text=%d thinking=%d tool=%d} max_tool_input_len=%d empty_tool_inputs=%d%s%s", protocol ? protocol : "-", id ? id : "-", model ? model : "-", stop ? stop : "-", input_tokens, output_tokens, total_tokens, raw_resp ? (int)strlen(raw_resp) : 0, text_blocks, thinking_blocks, tool_blocks, max_tool_input_len, empty_tool_inputs, empty_tool_name[0] ? " first_empty_tool=" : "", empty_tool_name[0] ? empty_tool_name : "");

	/* 空工具输入检测：保存原始响应到调试文件 */
	if (empty_tool_inputs <= 0 || !raw_resp) {
		return;
	}

	char dir[512];
	snprintf(dir, sizeof(dir), "%s/llm_debug", path_cache_dir());
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
		pr_warn("Failed to write LLM empty tool debug file: %s", path);
		return;
	}
	fwrite(raw_resp, 1, strlen(raw_resp), f);
	fclose(f);
	pr_warn("LLM empty tool input raw response saved: %s", path);
}

/**
 * 检测 URL 尾部是否为版本号路径（如 /v1 或 /v1.0）。
 * 用于判断 OpenAI API URL 是否需要拼接 chat/completions 后缀。
 *
 * @param url  API URL
 * @return     true 表示 URL 尾部是 /v1 或 /v1.0 格式
 */
static bool url_tail_is_version_root(const char *url)
{
	if (!url || !url[0]) {
		return false;
	}

	size_t len = strlen(url);
	/* 去除尾部斜杠 */
	while (len > 0 && url[len - 1] == '/') {
		len--;
	}
	if (len == 0) {
		return false;
	}

	/* 找到最后一个 / 之后的部分 */
	const char *tail = host_memrchr(url, '/', len);
	tail = tail ? tail + 1 : url;
	if ((size_t)(tail - url) >= len || !tail[0]) {
		return false;
	}

	/* 检测 v/V 开头后跟数字和点的模式 */
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

/**
 * 检测 base URL 是否为 DeepSeek 官方 API 地址。
 * DeepSeek 平台有特殊行为：仅支持 max_tokens 字段，且可能需要 Unicode 转义。
 */
static bool base_url_is_deepseek_official(const char *url)
{
	return url && strstr(url, "api.deepseek.com") != NULL;
}

/**
 * 判断 api_mode 是否应为 Anthropic Messages 协议。
 * 支持 "anthropic_messages" 和 "anthropic" 两种配置值。
 */
static bool api_mode_is_anthropic_messages(const char *api_mode)
{
	return api_mode &&
	       (strcasecmp(api_mode, "anthropic_messages") == 0 ||
	        strcasecmp(api_mode, "anthropic") == 0);
}

/**
 * 判断当前请求是否应使用 Anthropic Messages 协议。
 * 当前仅根据 api_mode 配置决定；model 和 base_url 参数保留供未来扩展。
 *
 * @param model    模型名称（保留参数）
 * @param base_url Base URL（保留参数）
 * @param api_mode API 模式配置值
 * @return         true 表示使用 Anthropic 协议
 */
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

/**
 * 规范化 OpenAI base URL，构造完整 API 端点路径。
 *
 * 拼接规则（按优先级）：
 * 1. 已含 /chat/completions → 直接使用
 * 2. Anthropic 协议 → 拼接 /v1/messages（DeepSeek 用 /anthropic/v1/messages）
 * 3. DeepSeek 官方 API → 直接拼接 /chat/completions
 * 4. URL 以 /v1 或 /v1.x 结尾 → 拼接 /chat/completions
 * 5. 其他 → 拼接 /v1/chat/completions
 *
 * 结果存储在全局 s_openai_api_url 中。
 */
static void build_openai_api_url(void)
{
	s_openai_api_url[0] = '\0';
	if (!s_openai_base_url[0]) {
		return;
	}

	const char *base = s_openai_base_url;
	/* 已包含完整路径：直接使用 */
	if (strstr(base, "/chat/completions")) {
		safe_copy(s_openai_api_url, sizeof(s_openai_api_url), base);
		return;
	}

	/* Anthropic Messages API 端点 */
	if (s_use_anthropic_api) {
		if (strstr(base, "/v1/messages")) {
			safe_copy(s_openai_api_url, sizeof(s_openai_api_url), base);
		} else if (strstr(base, "api.deepseek.com")) {
			/* DeepSeek 的 Anthropic 兼容端点路径特殊 */
			const char *suffix = strstr(base, "/anthropic") ? "v1/messages" : "anthropic/v1/messages";
			snprintf(s_openai_api_url, sizeof(s_openai_api_url),
			         str_ends_with(base, "/") ? "%s%s" : "%s/%s", base, suffix);
		} else if (str_ends_with(base, "/")) {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%sv1/messages", base);
		} else {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/v1/messages", base);
		}
		return;
	}

	/* OpenAI Chat Completions 端点 */
	if (base_url_is_deepseek_official(base)) {
		if (str_ends_with(base, "/")) {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%schat/completions", base);
		} else {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/chat/completions", base);
		}
		return;
	}

	/* 已含 /v1/ 或 /v1 结尾：直接追加 chat/completions */
	if (strstr(base, "/v1/") || str_ends_with(base, "/v1") || url_tail_is_version_root(base)) {
		if (str_ends_with(base, "/")) {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%schat/completions", base);
		} else {
			snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/chat/completions", base);
		}
		return;
	}

	/* 默认：补全 /v1/chat/completions 路径 */
	if (str_ends_with(base, "/")) {
		snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%sv1/chat/completions", base);
	} else {
		snprintf(s_openai_api_url, sizeof(s_openai_api_url), "%s/v1/chat/completions", base);
	}
}

/**
 * 判断是否应禁用思维链（thinking）。
 * 读取配置中的 thinking_mode 字段：
 * - "off" / "disabled" → true
 * - "omit" / "auto" → false（不禁用）
 * - 其他 → false
 */
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

/**
 * 获取当前请求的推理强度（reasoning_effort）。
 *
 * 解析优先级：
 * 1. 独立配置的 reasoning_effort 字段
 * 2. thinking_mode 映射：low/medium/high → 直接使用；on/enabled/auto → "medium"
 * 3. thinking_mode 为 off/disabled/omit → 返回 NULL
 *
 * @return 推理强度字符串；禁用时返回 NULL
 */
static const char *reasoning_effort_for_request(void)
{
	const char *mode = runtime_config_get_provider_thinking_mode();
	const char *effort = runtime_config_get_provider_reasoning_effort();
	/* thinking 被禁用时不返回推理强度 */
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
		/* on/enabled/auto → 默认中等推理强度 */
		if (strcasecmp(mode, "on") == 0 || strcasecmp(mode, "enabled") == 0 || strcasecmp(mode, "auto") == 0) {
			return "medium";
		}
	}
	return NULL;
}

/**
 * 是否需要额外的 reasoning_content 字段。
 * 部分 OpenAI 兼容 API（如某些国产模型）需要在 assistant 消息中显式传递 reasoning_content。
 */
static bool should_add_reasoning_content(void)
{
	return runtime_config_provider_needs_reasoning_content();
}

/**
 * 是否使用 "max_tokens" 而非 "max_completion_tokens"。
 * DeepSeek 平台不支持 max_completion_tokens，需回退到 max_tokens。
 */
static bool should_use_max_tokens_field(void)
{
	return base_url_is_deepseek_official(s_openai_base_url);
}

/**
 * 获取当前生效的 API URL。
 * 优先使用拼接后的 s_openai_api_url，未设置时回退到编译期常量 OPENAI_API_URL。
 */
static const char *llm_api_url(void)
{
	if (s_openai_api_url[0]) {
		return s_openai_api_url;
	}
	return OPENAI_API_URL;
}

/**
 * 构建完整的 JSON 请求体。
 *
 * 根据 s_use_anthropic_api 调用对应的载荷构造函数（OpenAI / Anthropic）。
 * 对于 Anthropic 协议，额外处理 DeepSeek 的 Unicode 转义需求（非 ASCII 字符 → \uXXXX）。
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组
 * @param tools_json     工具定义 JSON
 * @param model_name     模型名称
 * @return               堆分配的 JSON 字符串（调用者负责 kfree）；失败返回 NULL
 */
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

	/* DeepSeek Anthropic API 兼容处理：将非 ASCII 字符转义为 \uXXXX。
	 * 部分 DeepSeek 端点对包含中文等非 ASCII 字符的 JSON 返回 400 错误。 */
	if (post_data && s_use_anthropic_api) {
		size_t len = strlen(post_data);
		size_t escaped_len = 0;
		for (size_t i = 0; i < len; i++) {
			unsigned char c = (unsigned char)post_data[i];
			escaped_len += (c > 127) ? 6 : 1;  /* \uXXXX = 6 字符 */
		}
		if (escaped_len > len) {
			char *escaped = kmalloc(escaped_len + 1, GFP_KERNEL);
			if (escaped) {
				size_t j = 0;
				for (size_t i = 0; i < len; i++) {
					unsigned char c = (unsigned char)post_data[i];
					if (c > 127) {
						/* 解码完整 UTF-8 序列获取 Unicode code point */
						unsigned int cp = c;
						int trail = 0;
						if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; trail = 1; }
						else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; trail = 2; }
						else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; trail = 3; }
						for (int t = 0; t < trail && i + 1 + t < len; t++)
							cp = (cp << 6) | ((unsigned char)post_data[i + 1 + t] & 0x3F);
						i += trail;  /* 跳过后续字节 */
						j += snprintf(escaped + j, 7, "\\u%04X", cp);
					} else {
						escaped[j++] = c;
					}
				}
				escaped[j] = '\0';
				kfree(post_data);
				post_data = escaped;
			}
		}
	}
	return post_data;
}

/**
 * 构建 HTTP 请求头链表。
 *
 * 认证头规则：
 * - 包含 "/anthropic/" 的 URL → x-api-key + anthropic-version: 2023-06-01
 * - 其他 URL → Authorization: Bearer <api_key>
 *
 * @param url  请求 URL（用于判断认证头格式）
 * @return     堆分配的 curl_slist 链表；调用者负责通过 curl_slist_free_all 释放
 */
static struct curl_slist *build_headers(const char *url)
{
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	/* Anthropic 协议认证头：x-api-key（非 Authorization: Bearer） */
	if (s_api_key[0] && url && strstr(url, "/anthropic/")) {
		char key_header[LLM_AUTH_HEADER_MAX];
		snprintf(key_header, sizeof(key_header), "x-api-key: %s", s_api_key);
		headers = curl_slist_append(headers, key_header);
		headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
	} else if (s_api_key[0]) {
		/* OpenAI 兼容协议认证头：Authorization: Bearer */
		char auth[LLM_AUTH_HEADER_MAX];
		snprintf(auth, sizeof(auth), "Authorization: Bearer %s", s_api_key);
		headers = curl_slist_append(headers, auth);
	}

	return headers;
}

/**
 * 解析 LLM API 响应。
 *
 * 流程：
 * 1. 检查 HTTP 状态码 ≠ 200 → 失败
 * 2. 执行诊断日志（token 统计、空工具输入检测）
 * 3. 根据协议选择对应的解析函数（OpenAI / Anthropic）
 * 4. 输出响应摘要（文本字节数、工具调用数、停止原因）
 *
 * @param raw_resp           原始响应文本
 * @param status             HTTP 状态码
 * @param use_anthropic_api  是否使用 Anthropic 协议
 * @param resp               输出响应结构体（重置后填充）
 * @return                   成功返回 0
 */
static err_t parse_llm_response(const char *raw_resp,
                                      long status,
                                      bool use_anthropic_api,
                                      llm_response_t *resp)
{
	if (!resp) {
		return ERR_INVALID_ARG;
	}
	memset(resp, 0, sizeof(*resp));

	if (status != 200) {
		pr_err("API error %ld: %.500s", status, raw_resp ? raw_resp : "");
		return ERR_FAIL;
	}

	/* 诊断日志：统计响应结构和 token 消耗 */
	cJSON *diag_root = cJSON_Parse(raw_resp);
	if (diag_root) {
		log_llm_response_diagnostics(
			use_anthropic_api ? "anthropic-compatible" : "openai-compatible",
			raw_resp,
			diag_root);
		cJSON_Delete(diag_root);
	} else {
		pr_warn("LLM diagnostics skipped: raw response JSON parse failed");
	}

	err_t err = use_anthropic_api
		? llm_anthropic_parse_response(raw_resp, resp)
		: llm_openai_parse_response(raw_resp, resp);
	if (err != 0) {
		pr_err("Failed to parse API response JSON");
		return err;
	}

	pr_info("Response: %d bytes text, %d tool calls, stop=%s", (int)resp->text_len, resp->call_count, resp->tool_use ? "tool_use" : "end_turn");
	if (resp->text && resp->text[0]) {
		pr_info("LLM text: %zu bytes", strlen(resp->text));
	}

	return 0;
}

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
		pr_warn("No API key configured in %s", path_runtime_config_file());
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

	char *post_data = build_request_body(system_prompt, messages, tools_json, s_model);
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
	if (!model_override || !model_override[0]) {
		return llm_chat_tools(system_prompt, messages, tools_json, resp);
	}

	/* 保存并临时切换模型，完成后恢复 */
	char previous_model[LLM_MODEL_MAX_LEN];
	safe_copy(previous_model, sizeof(previous_model), s_model);
	safe_copy(s_model, sizeof(s_model), model_override);
	err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
	safe_copy(s_model, sizeof(s_model), previous_model);
	return err;
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
	                                     chat->model_name);
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
