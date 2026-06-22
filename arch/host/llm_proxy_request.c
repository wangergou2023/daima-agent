/* Host 平台 LLM 代理 — HTTP 请求/响应处理。
 *
 * 职责：构建 JSON 请求体、HTTP 头、解析 LLM API 响应。
 */

#include "drivers/llm/llm_proxy.h"
#include "runtime.h"
#include "drivers/llm/llm_anthropic_payload.h"
#include "drivers/llm/llm_openai_payload.h"
#include "cjson.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/printk.h"

#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

#define LLM_AUTH_HEADER_MAX 352

/* ── 共享全局状态（由 llm_proxy_host.c 定义） ── */
extern char s_api_key[];
extern char s_model[];
extern bool s_use_anthropic_api;

/* ── 由 llm_proxy_routing.c 提供的函数 ── */
extern void log_llm_response_diagnostics(const char *protocol,
                                         const char *raw_resp,
                                         cJSON *root);
extern bool should_disable_thinking(void);
extern const char *reasoning_effort_for_request(void);
extern bool should_add_reasoning_content(void);
extern bool should_use_max_tokens_field(void);

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
char *build_request_body(const char *system_prompt,
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
			if (c < 0x80) {
				escaped_len += 1;
				continue;
			}

			int trail = 0;
			if ((c & 0xE0) == 0xC0) trail = 1;
			else if ((c & 0xF0) == 0xE0) trail = 2;
			else if ((c & 0xF8) == 0xF0) trail = 3;

			unsigned int cp = c;
			if (trail == 1) cp = c & 0x1F;
			else if (trail == 2) cp = c & 0x0F;
			else if (trail == 3) cp = c & 0x07;

			bool valid = trail > 0 && i + (size_t)trail < len;
			for (int t = 0; valid && t < trail; t++) {
				unsigned char cont = (unsigned char)post_data[i + 1 + t];
				if ((cont & 0xC0) != 0x80) {
					valid = false;
					break;
				}
				cp = (cp << 6) | (cont & 0x3F);
			}

			if (!valid) {
				escaped_len += 1;
				continue;
			}

			escaped_len += (cp <= 0xFFFFU) ? 6 : 12;
			i += (size_t)trail;
		}
		if (escaped_len > len) {
			char *escaped = kmalloc(escaped_len + 1, GFP_KERNEL);
			if (escaped) {
				size_t j = 0;
				for (size_t i = 0; i < len; i++) {
					unsigned char c = (unsigned char)post_data[i];
					if (c < 0x80) {
						escaped[j++] = c;
						continue;
					}

					unsigned int cp = c;
					int trail = 0;
					if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; trail = 1; }
					else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; trail = 2; }
					else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; trail = 3; }

					bool valid = trail > 0 && i + (size_t)trail < len;
					for (int t = 0; valid && t < trail; t++) {
						unsigned char cont = (unsigned char)post_data[i + 1 + t];
						if ((cont & 0xC0) != 0x80) {
							valid = false;
							break;
						}
						cp = (cp << 6) | (cont & 0x3F);
					}

					if (!valid) {
						escaped[j++] = c;
						continue;
					}

					i += (size_t)trail;  /* 跳过后续字节 */
					if (cp <= 0xFFFFU) {
						j += snprintf(escaped + j, escaped_len + 1 - j, "\\u%04X", cp);
					} else {
						unsigned int v = cp - 0x10000U;
						unsigned int high = 0xD800U + (v >> 10);
						unsigned int low = 0xDC00U + (v & 0x3FFU);
						j += snprintf(escaped + j, escaped_len + 1 - j,
							      "\\u%04X\\u%04X", high, low);
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
struct curl_slist *build_headers(const char *url)
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
err_t parse_llm_response(const char *raw_resp,
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
