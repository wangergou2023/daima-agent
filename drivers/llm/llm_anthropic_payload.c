/* Anthropic Messages API 协议层实现。
 * - 构造符合 Anthropic Messages API 规范的 JSON 请求体
 * - 解析 Anthropic 格式的 JSON 响应，提取文本/工具调用/推理内容
 * - 与 OpenAI 协议的核心差异：
 *   1. system 是请求顶层字段（非 messages 数组内）
 *   2. tool_choice 是对象 {"type":"auto"}（非字符串 "auto"）
 *   3. content 始终是块数组（非字符串）
 *   4. tool_use 块的 input 是 JSON 对象（非序列化字符串）
 *   5. 无 finish_reason，用 stop_reason 替代
 *   6. reasoning/thinking 块合并为统一的 thinking 类型
 */

#include "drivers/llm/llm_anthropic_payload.h"

#include <stdlib.h>
#include <string.h>

#include "context_build.h"
#include "cjson.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "linux/slab.h"

/* 记录解析到的工具调用详情日志。与 OpenAI 版本共用相同格式。 */
static void log_tool_call_parse(const char *protocol,
                                int index,
                                const char *id,
                                const char *name,
                                const char *input_json)
{
	char preview[320];
	const char *state = "value";
	const char *src = input_json ? input_json : "";
	size_t n = input_json ? strlen(input_json) : 0;
	if (!input_json) {
		state = "null";
	} else if (strcmp(input_json, "{}") == 0) {
		state = "empty_object";
	}
	size_t shown = n > sizeof(preview) - 1 ? sizeof(preview) - 1 : n;
	memcpy(preview, src, shown);
	preview[shown] = '\0';
	for (size_t i = 0; i < shown; i++) {
		if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
			preview[i] = ' ';
		}
	}
	pr_info("%s parsed tool_call[%d]: id=%s name=%s input_state=%s input_len=%u input=%s%s", protocol, index, id && id[0] ? id : "<missing>", name && name[0] ? name : "<missing>", state, (unsigned)n, preview[0] ? preview : "<empty>", n > shown ? "..." : "");
}

/**
 * 修复截断的 UTF-8 序列。
 */
static void sanitize_utf8(char *s)
{
	if (!s) return;
	context_fix_truncated_utf8(s, strlen(s));
}

/**
 * 直接复制工具定义 JSON（Anthropic 协议已与内部格式兼容）。
 *
 * Anthropic 工具格式：{"name":"...", "description":"...", "input_schema":{...}}
 * 与内部存储格式完全一致，无需字段转换。与 OpenAI 的 convert_tools_openai 不同，
 * Anthropic 不需要包装 type/function 外层。
 *
 * @param tools_json  工具定义 JSON 数组字符串
 * @return            解析后的 cJSON 数组（转移所有权，调用者负责释放）
 */
static cJSON *duplicate_tools_anthropic(const char *tools_json)
{
	if (!tools_json) return NULL;
	cJSON *tools = cJSON_Parse(tools_json);
	if (!tools || !cJSON_IsArray(tools)) {
		cJSON_Delete(tools);
		return NULL;
	}
	return tools;
}

/**
 * 将内部消息数组转换为 Anthropic Messages 格式。
 *
 * Anthropic 消息格式要求：
 * - content 始终是块数组 [{type:"text"|"tool_use"|"tool_result"|"thinking"}, ...]
 * - 不支持纯字符串 content
 * - system 角色在转换时被丢弃（system 提示词由调用者放在顶层 "system" 字段）
 * - reasoning/thinking 块统一转为 thinking 类型
 * - tool_use/tool_result 块直接透传（内部格式已兼容 Anthropic）
 *
 * @param messages  内部格式消息数组
 * @return          转换后的消息数组（仅含 user/assistant 角色）
 */
static cJSON *convert_messages_anthropic(cJSON *messages)
{
	cJSON *out = cJSON_CreateArray();
	if (!out || !messages || !cJSON_IsArray(messages)) {
		return out;
	}

	cJSON *msg = NULL;
	cJSON_ArrayForEach(msg, messages) {
		cJSON *role = cJSON_GetObjectItem(msg, "role");
		cJSON *content = cJSON_GetObjectItem(msg, "content");
		if (!role || !cJSON_IsString(role) || !content) {
			continue;
		}
		/* Anthropic 的消息数组不包含 system 角色——system 是请求顶层字段 */
		if (strcmp(role->valuestring, "system") == 0) {
			continue;
		}

		cJSON *m = cJSON_CreateObject();
		if (!m) {
			continue;
		}
		cJSON_AddStringToObject(m, "role", role->valuestring);

		/* 简单字符串 content：包装为单元素块数组 [{"type":"text","text":"..."}] */
		if (cJSON_IsString(content)) {
			sanitize_utf8(content->valuestring);
			cJSON_AddStringToObject(m, "content", content->valuestring);
			cJSON_AddItemToArray(out, m);
			continue;
		}

		/* 非数组 content：跳过 */
		if (!cJSON_IsArray(content)) {
			cJSON_Delete(m);
			continue;
		}

		/* ── 块数组 content：逐个处理块类型 ── */
		cJSON *blocks = cJSON_CreateArray();
		if (!blocks) {
			cJSON_Delete(m);
			continue;
		}

		cJSON *block = NULL;
		cJSON_ArrayForEach(block, content) {
			cJSON *btype = cJSON_GetObjectItem(block, "type");
			if (!btype || !cJSON_IsString(btype)) {
				continue;
			}

			/* 支持的块类型：text / reasoning / thinking / tool_use / tool_result
			 * 这些类型与 Anthropic API 完全兼容，直接复制 */
			if (strcmp(btype->valuestring, "text") == 0 ||
			    strcmp(btype->valuestring, "reasoning") == 0 ||
			    strcmp(btype->valuestring, "thinking") == 0 ||
			    strcmp(btype->valuestring, "tool_use") == 0 ||
			    strcmp(btype->valuestring, "tool_result") == 0) {
				cJSON *dup = cJSON_Duplicate(block, 1);
				/* reasoning 块：需要转换为 Anthropic 的 thinking 类型。
				 * 提取文本后替换 type 为 "thinking"，重命名字段 text → thinking */
				if (dup && (strcmp(btype->valuestring, "reasoning") == 0 ||
				            strcmp(btype->valuestring, "thinking") == 0)) {
					cJSON *text = cJSON_GetObjectItemCaseSensitive(dup, "text");
					cJSON *thinking = cJSON_GetObjectItemCaseSensitive(dup, "thinking");
					char *value_copy = NULL;
					/* 优先取 thinking 字段，其次 text 字段 */
					if (thinking && cJSON_IsString(thinking)) {
						value_copy = strdup(thinking->valuestring);
					} else if (text && cJSON_IsString(text)) {
						value_copy = strdup(text->valuestring);
					}
					/* 统一替换为 thinking 类型 */
					cJSON_ReplaceItemInObjectCaseSensitive(dup,
					                                       "type",
					                                       cJSON_CreateString("thinking"));
					/* 移除旧字段，避免冲突 */
					cJSON_DeleteItemFromObjectCaseSensitive(dup, "text");
					cJSON_DeleteItemFromObjectCaseSensitive(dup, "thinking");
					if (value_copy && value_copy[0]) {
						cJSON_AddStringToObject(dup, "thinking", value_copy);
					}
					kfree(value_copy);
				}
				if (dup) {
					cJSON_AddItemToArray(blocks, dup);
				}
			}
		}

		cJSON_AddItemToObject(m, "content", blocks);
		cJSON_AddItemToArray(out, m);
	}
	return out;
}

/**
 * 构建完整的 Anthropic Messages API JSON 请求体。
 *
 * 顶层 JSON 结构：
 * {
 *   "model": "...",
 *   "max_tokens": N,                                     // Anthropic 仅支持 max_tokens
 *   "system": "系统提示词",                               // 顶层字符串（非 messages 数组内）
 *   "messages": [{"role":"user"|"assistant", "content":[{...},...]}, ...],
 *   "tools": [{"name":"...", "description":"...", "input_schema":{...}}, ...],
 *   "tool_choice": {"type": "auto"},                     // 对象格式（非字符串）
 *   "thinking": {"type": "disabled"} | {"type": "enabled"},  // 可选
 *   "output_config": {"effort": "low"|"medium"|"high"}   // 与 thinking.enabled 配对
 * }
 *
 * @param system_prompt     系统提示词
 * @param messages           历史消息数组
 * @param tools_json         工具定义 JSON 数组
 * @param model              模型名称
 * @param max_tokens         最大输出 token 数
 * @param disable_thinking   禁止思维链
 * @param reasoning_effort   推理强度（非空时设置 output_config.effort）
 * @return                   堆分配的 cJSON 请求体
 */
cJSON *llm_anthropic_build_tools_body(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort,
                                      bool response_format_json_object)
{
	cJSON *body = cJSON_CreateObject();
	if (!body) {
		return NULL;
	}

	cJSON_AddStringToObject(body, "model", model ? model : "");
	cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
	/* thinking/reasoning 控制：
	 * - disabled → thinking.type="disabled"
	 * - enabled → thinking.type="enabled" + output_config.effort=reasoning_effort */
	if (disable_thinking) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "disabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
	} else if (reasoning_effort && reasoning_effort[0]) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "enabled");
		cJSON_AddItemToObject(body, "thinking", thinking);

		/* Anthropic 使用 output_config 设置推理强度（非 reasoning_effort 顶层字段） */
		cJSON *output_config = cJSON_CreateObject();
		cJSON_AddStringToObject(output_config, "effort", reasoning_effort);
		cJSON_AddItemToObject(body, "output_config", output_config);
	}
	/* system 提示词：Anthropic 协议中 system 是顶层字段（非 messages 数组的元素） */
	if (system_prompt && system_prompt[0]) {
		cJSON_AddStringToObject(body, "system", system_prompt);
	}
	cJSON_AddItemToObject(body, "messages", convert_messages_anthropic(messages));

	/* 工具调用：
	 * - tools 直接使用内部的 input_schema 格式（无需转换）
	 * - tool_choice 为对象 {"type":"auto"}（OpenAI 为字符串 "auto"） */
	if (tools_json) {
		cJSON *tools = duplicate_tools_anthropic(tools_json);
		if (tools) {
			cJSON_AddItemToObject(body, "tools", tools);
			cJSON *choice = cJSON_CreateObject();
			if (choice) {
				cJSON_AddStringToObject(choice, "type", "auto");
				cJSON_AddItemToObject(body, "tool_choice", choice);
			}
		}
	}
	if (response_format_json_object) {
		cJSON *response_format = cJSON_CreateObject();
		if (response_format) {
			cJSON_AddStringToObject(response_format, "type", "json_object");
			cJSON_AddItemToObject(body, "response_format", response_format);
		}
	}
	return body;
}

#ifdef ENABLE_VISION
/**
 * 构建 Anthropic 视觉理解请求体。
 *
 * 当前实现为存根：图片参数被忽略，仅构建纯文本 user 消息。
 * 完整实现需将 content 构建为 [{type:"image", source:{type:"base64", media_type:"...", data:"..."}}, ...]。
 *
 * @param system_prompt     系统提示词
 * @param user_text         用户文本
 * @param images            图片数组（当前未使用）
 * @param image_count       图片数量（当前未使用）
 * @param model             模型名称
 * @param max_tokens        最大输出 token 数
 * @param disable_thinking  禁止思维链
 * @param reasoning_effort  推理强度
 * @return                  堆分配的 cJSON 请求体
 */
cJSON *llm_anthropic_build_image_body(const char *system_prompt,
                                      const char *user_text,
                                      const llm_image_content_t *images,
                                      int image_count,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort)
{
	(void)images;
	(void)image_count;
	cJSON *body = cJSON_CreateObject();
	if (!body) {
		return NULL;
	}
	cJSON_AddStringToObject(body, "model", model ? model : "");
	cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
	if (disable_thinking) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "disabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
	} else if (reasoning_effort && reasoning_effort[0]) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "enabled");
		cJSON_AddItemToObject(body, "thinking", thinking);

		cJSON *output_config = cJSON_CreateObject();
		cJSON_AddStringToObject(output_config, "effort", reasoning_effort);
		cJSON_AddItemToObject(body, "output_config", output_config);
	}
	/* system 提示词：Anthropic 顶层字段 */
	if (system_prompt && system_prompt[0]) {
		cJSON_AddStringToObject(body, "system", system_prompt);
	}

	/* 纯文本 user 消息（存根实现，图片参数未使用） */
	cJSON *messages = cJSON_CreateArray();
	cJSON *msg = cJSON_CreateObject();
	if (messages && msg) {
		cJSON_AddStringToObject(msg, "role", "user");
		cJSON_AddStringToObject(msg, "content", user_text ? user_text : "");
		cJSON_AddItemToArray(messages, msg);
		cJSON_AddItemToObject(body, "messages", messages);
	} else {
		cJSON_Delete(messages);
		cJSON_Delete(msg);
		cJSON_Delete(body);
		return NULL;
	}
	return body;
}
#endif

/**
 * 解析 Anthropic Messages API 响应 JSON，提取到 llm_response_t 结构体。
 *
 * 解析目标 JSON 结构：
 * {
 *   "stop_reason": "end_turn" | "tool_use",          // 结束原因
 *   "content": [
 *     {"type": "text", "text": "..."},               // 文本块
 *     {"type": "thinking", "thinking": "..."},       // 推理块（thinking 或 text 字段）
 *     {"type": "tool_use", "id":"...", "name":"...", "input":{...}}  // 工具调用块
 *   ]
 * }
 *
 * 与 OpenAI 解析的关键差异：
 * - content 是顶层数组（非 choices[0].message.content）
 * - 多个 text 块拼接为一个字符串
 * - tool_use 的 input 是 JSON 对象（调用 cJSON_PrintUnformatted 序列化）
 * - stop_reason 替代 finish_reason
 *
 * @param json_text  API 响应 JSON 原文
 * @param resp       输出响应结构体（重置后填充，堆内存由调用者释放）
 * @return           成功返回 0
 */
err_t llm_anthropic_parse_response(const char *json_text, llm_response_t *resp)
{
	if (!json_text || !resp) {
		return ERR_INVALID_ARG;
	}

	cJSON *root = cJSON_Parse(json_text);
	if (!root) {
		return ERR_FAIL;
	}

	/* stop_reason 判断：
	 * - "tool_use" → 模型请求调用工具
	 * - "end_turn"  → 对话结束 */
	cJSON *stop = cJSON_GetObjectItem(root, "stop_reason");
	if (stop && cJSON_IsString(stop)) {
		resp->tool_use = strcmp(stop->valuestring, "tool_use") == 0;
	}

	/* 解析 content 数组（每个块可能是 text / thinking / reasoning / tool_use） */
	cJSON *content = cJSON_GetObjectItem(root, "content");
	if (content && cJSON_IsArray(content)) {
		cJSON *block = NULL;
		cJSON_ArrayForEach(block, content) {
			cJSON *type = cJSON_GetObjectItem(block, "type");
			if (!type || !cJSON_IsString(type)) {
				continue;
			}

			if (strcmp(type->valuestring, "text") == 0) {
				/* text 块：拼接到响应文本（支持多个 text 块） */
				cJSON *text = cJSON_GetObjectItem(block, "text");
				if (text && cJSON_IsString(text)) {
					size_t old_len = resp->text_len;
					size_t add_len = strlen(text->valuestring);
					char *next = realloc(resp->text, old_len + add_len + 1);
					if (next) {
						resp->text = next;
						memcpy(resp->text + old_len, text->valuestring, add_len);
						resp->text[old_len + add_len] = '\0';
						resp->text_len = old_len + add_len;
					}
				}
				continue;
			}

			if (strcmp(type->valuestring, "thinking") == 0 ||
			    strcmp(type->valuestring, "reasoning") == 0) {
				/* thinking/reasoning 块：取 thinking 字段（回退到 text 字段） */
				cJSON *thinking = cJSON_GetObjectItem(block, "thinking");
				if (!thinking || !cJSON_IsString(thinking)) {
					thinking = cJSON_GetObjectItem(block, "text");
				}
				if (thinking && cJSON_IsString(thinking) && thinking->valuestring[0]) {
					size_t rlen = strlen(thinking->valuestring);
					kfree(resp->reasoning_content);
					resp->reasoning_content = kzalloc(rlen + 1, GFP_KERNEL);
					if (resp->reasoning_content) {
						memcpy(resp->reasoning_content, thinking->valuestring, rlen);
						resp->reasoning_content_len = rlen;
					}
				}
				continue;
			}

			if (strcmp(type->valuestring, "tool_use") == 0 &&
			    resp->call_count < MAX_TOOL_CALLS) {
				/* tool_use 块：
				 * - id 为工具调用唯一标识（用于 tool_result 关联）
				 * - name 为工具名称
				 * - input 为 JSON 对象（Anthropic 原生格式），需序列化为字符串 */
				llm_tool_call_t *call = &resp->calls[resp->call_count];
				cJSON *id = cJSON_GetObjectItem(block, "id");
				cJSON *name = cJSON_GetObjectItem(block, "name");
				cJSON *input = cJSON_GetObjectItem(block, "input");
				if (id && cJSON_IsString(id)) {
					strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
				}
				if (name && cJSON_IsString(name)) {
					strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
				}
				if (input) {
					/* Anthropic 的 input 是 JSON 对象，序列化为字符串与内部统一格式兼容 */
					call->input = cJSON_PrintUnformatted(input);
					if (call->input) {
						call->input_len = strlen(call->input);
					}
				}
				log_tool_call_parse("anthropic",
				                    resp->call_count,
				                    call->id,
				                    call->name,
				                    call->input);
				resp->call_count++;
				resp->tool_use = true;
			}
		}
	}

	cJSON_Delete(root);
	return 0;
}
