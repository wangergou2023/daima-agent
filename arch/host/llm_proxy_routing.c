/* Host 平台 LLM 代理 — URL 路由与协议选择。
 *
 * 职责：URL 拼接、平台检测（DeepSeek）、thinking 开关逻辑、
 *       响应诊断日志（token 统计、空工具输入检测）。
 */

#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "autoconf.h"
#include "cjson.h"
#include "json_helpers.h"
#include "paths.h"
#include "text.h"
#include "runtime.h"
#include "arch/host/portability.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

/* ── 共享全局状态（由 llm_proxy_host.c 定义） ── */
extern char s_openai_base_url[BUF_SMALL];
extern char s_openai_api_url[BUF_MEDIUM];
extern bool s_use_anthropic_api;
extern unsigned s_llm_debug_seq;

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
void log_llm_response_diagnostics(const char *protocol,
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
bool base_url_is_deepseek_official(const char *url)
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
bool should_use_anthropic_messages(const char *model,
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
void build_api_url_for(const char *base, bool use_anthropic_api, char *out, size_t out_size)
{
	if (!out || out_size == 0) {
		return;
	}
	out[0] = '\0';
	if (!base || !base[0]) {
		return;
	}
	/* 已包含完整路径：直接使用 */
	if (strstr(base, "/chat/completions")) {
		strscpy(out, base, out_size);
		return;
	}

	/* Anthropic Messages API 端点 */
	if (use_anthropic_api) {
		if (strstr(base, "/v1/messages")) {
			strscpy(out, base, out_size);
		} else if (strstr(base, "api.deepseek.com")) {
			/* DeepSeek 的 Anthropic 兼容端点路径特殊 */
			const char *suffix = strstr(base, "/anthropic") ? "v1/messages" : "anthropic/v1/messages";
			snprintf(out, out_size, str_ends_with(base, "/") ? "%s%s" : "%s/%s", base, suffix);
		} else if (str_ends_with(base, "/")) {
			snprintf(out, out_size, "%sv1/messages", base);
		} else {
			snprintf(out, out_size, "%s/v1/messages", base);
		}
		return;
	}

	/* OpenAI Chat Completions 端点 */
	if (base_url_is_deepseek_official(base)) {
		if (str_ends_with(base, "/")) {
			snprintf(out, out_size, "%schat/completions", base);
		} else {
			snprintf(out, out_size, "%s/chat/completions", base);
		}
		return;
	}

	/* 已含 /v1/ 或 /v1 结尾：直接追加 chat/completions */
	if (strstr(base, "/v1/") || str_ends_with(base, "/v1") || url_tail_is_version_root(base)) {
		if (str_ends_with(base, "/")) {
			snprintf(out, out_size, "%schat/completions", base);
		} else {
			snprintf(out, out_size, "%s/chat/completions", base);
		}
		return;
	}

	/* 默认：补全 /v1/chat/completions 路径 */
	if (str_ends_with(base, "/")) {
		snprintf(out, out_size, "%sv1/chat/completions", base);
	} else {
		snprintf(out, out_size, "%s/v1/chat/completions", base);
	}
}

void build_openai_api_url(void)
{
	build_api_url_for(s_openai_base_url, s_use_anthropic_api, s_openai_api_url, sizeof(s_openai_api_url));
}

/**
 * 判断是否应禁用思维链（thinking）。
 * 读取配置中的 thinking_mode 字段：
 * - "off" / "disabled" → true
 * - "omit" / "auto" → false（不禁用）
 * - 其他 → false
 */
bool should_disable_thinking_for_mode(const char *thinking_mode)
{
	if (thinking_mode && thinking_mode[0]) {
		if (strcasecmp(thinking_mode, "off") == 0 || strcasecmp(thinking_mode, "disabled") == 0) {
			return true;
		}
		if (strcasecmp(thinking_mode, "omit") == 0 || strcasecmp(thinking_mode, "auto") == 0) {
			return false;
		}
	}
	return false;
}

bool should_disable_thinking(void)
{
	return should_disable_thinking_for_mode(runtime_config_get_provider_thinking_mode());
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
const char *reasoning_effort_for_request_with_values(const char *thinking_mode,
                                                     const char *reasoning_effort)
{
	/* thinking 被禁用时不返回推理强度 */
	if (thinking_mode && thinking_mode[0] &&
	    (strcasecmp(thinking_mode, "off") == 0 || strcasecmp(thinking_mode, "disabled") == 0 ||
	     strcasecmp(thinking_mode, "omit") == 0)) {
		return NULL;
	}
	if (reasoning_effort && reasoning_effort[0]) {
		return reasoning_effort;
	}
	if (thinking_mode && thinking_mode[0]) {
		if (strcasecmp(thinking_mode, "low") == 0 || strcasecmp(thinking_mode, "medium") == 0 || strcasecmp(thinking_mode, "high") == 0) {
			return thinking_mode;
		}
		/* on/enabled/auto → 默认中等推理强度 */
		if (strcasecmp(thinking_mode, "on") == 0 || strcasecmp(thinking_mode, "enabled") == 0 || strcasecmp(thinking_mode, "auto") == 0) {
			return "medium";
		}
	}
	return NULL;
}

const char *reasoning_effort_for_request(void)
{
	return reasoning_effort_for_request_with_values(runtime_config_get_provider_thinking_mode(),
	                                                runtime_config_get_provider_reasoning_effort());
}

/**
 * 是否需要额外的 reasoning_content 字段。
 * 部分 OpenAI 兼容 API（如某些国产模型）需要在 assistant 消息中显式传递 reasoning_content。
 */
bool should_add_reasoning_content(void)
{
	return runtime_config_provider_needs_reasoning_content();
}

/**
 * 是否使用 "max_tokens" 而非 "max_completion_tokens"。
 * DeepSeek 平台不支持 max_completion_tokens，需回退到 max_tokens。
 */
bool should_use_max_tokens_field_for_base_url(const char *base_url)
{
	return base_url_is_deepseek_official(base_url);
}

bool should_use_max_tokens_field(void)
{
	return should_use_max_tokens_field_for_base_url(s_openai_base_url);
}

/**
 * 获取当前生效的 API URL。
 * 优先使用拼接后的 s_openai_api_url，未设置时回退到编译期常量 OPENAI_API_URL。
 */
const char *llm_api_url(void)
{
	if (s_openai_api_url[0]) {
		return s_openai_api_url;
	}
	return OPENAI_API_URL;
}
