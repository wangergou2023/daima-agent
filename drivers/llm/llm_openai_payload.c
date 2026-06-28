/* OpenAI Chat Completions 协议层实现。
 * - 构造符合 OpenAI /v1/chat/completions 规范的 JSON 请求体
 * - 解析 OpenAI 格式的 JSON 响应，提取文本/工具调用/推理内容
 * - 处理三种消息格式转换：内部块格式（content 数组）→ OpenAI 消息格式
 * - 支持 function calling（tools/tool_choice）、thinking/reasoning 扩展字段
 */

#include "drivers/llm/llm_openai_payload.h"
#include "context_build.h"

#include <stdlib.h>
#include <string.h>

#include "linux/printk.h"
#include "linux/slab.h"

/* 记录解析到的工具调用详情日志。用于调试函数调用参数。 */
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
	/* 将控制字符替换为空格，避免日志换行混乱 */
	for (size_t i = 0; i < shown; i++) {
		if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
			preview[i] = ' ';
		}
	}
	pr_info("%s parsed tool_call[%d]: id=%s name=%s input_state=%s input_len=%u input=%s%s", protocol, index, id && id[0] ? id : "<missing>", name && name[0] ? name : "<missing>", state, (unsigned)n, preview[0] ? preview : "<empty>", n > shown ? "..." : "");
}

/**
 * 原地修复截断的 UTF-8 序列。
 * 用于确保传输给 API 的字符串为合法 UTF-8，避免 JSON 编码错误。
 */
static void sanitize_utf8(char *s)
{
	if (!s) return;
	size_t len = strlen(s);
	context_fix_truncated_utf8(s, len);
}

/**
 * 将内部工具定义 JSON 转换为 OpenAI function calling 格式。
 *
 * 输入格式（内部/Anthropic 兼容）：
 * [{"name":"tool_name", "description":"...", "input_schema":{...}}, ...]
 *
 * 输出格式（OpenAI）：
 * [{"type":"function", "function":{"name":"...", "description":"...", "parameters":{...}}}, ...]
 *
 * @param tools_json  内部工具定义 JSON 数组字符串
 * @return            转换后的 cJSON 数组；失败或空指针返回 NULL
 */
static cJSON *convert_tools_openai(const char *tools_json)
{
	if (!tools_json) return NULL;
	cJSON *arr = cJSON_Parse(tools_json);
	if (!arr || !cJSON_IsArray(arr)) {
		cJSON_Delete(arr);
		return NULL;
	}

	cJSON *out = cJSON_CreateArray();
	cJSON *tool = NULL;
	cJSON_ArrayForEach(tool, arr) {
		/* 提取内部工具定义的 name/description/input_schema */
		cJSON *name = cJSON_GetObjectItem(tool, "name");
		cJSON *desc = cJSON_GetObjectItem(tool, "description");
		cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");
		if (!name || !cJSON_IsString(name)) {
			continue;
		}

		/* 构造 OpenAI function 对象 */
		cJSON *func = cJSON_CreateObject();
		cJSON_AddStringToObject(func, "name", name->valuestring);
		if (desc && cJSON_IsString(desc)) {
			cJSON_AddStringToObject(func, "description", desc->valuestring);
		}
		if (schema) {
			/* input_schema → parameters（OpenAI 对 JSON Schema 的字段名） */
			cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, 1));
		}

		/* 包装为 {"type":"function", "function":{...}} */
		cJSON *wrap = cJSON_CreateObject();
		cJSON_AddStringToObject(wrap, "type", "function");
		cJSON_AddItemToObject(wrap, "function", func);
		cJSON_AddItemToArray(out, wrap);
	}

	cJSON_Delete(arr);
	return out;
}

/**
 * 将内部消息数组转换为 OpenAI Chat Completions 消息格式。
 *
 * 内部消息格式：content 是块数组 [{type:"text"|"tool_use"|"tool_result"|"image_url"|"reasoning"}, ...]
 * OpenAI 消息格式：content 为字符串或（仅 user 消息）多模态数组
 *
 * 转换规则：
 * - system 角色 → {"role":"system", "content":"..."}
 * - assistant 角色 → {"role":"assistant", "content":"文本拼接", "tool_calls":[{...}], "reasoning_content":"..."}
 * - user 角色 → {"role":"user", "content":"文本"|[{type:"image_url",...},{type:"text",...}]}
 * - tool_result 块 → {"role":"tool", "content":"...", "tool_call_id":"..."}
 *
 * @param system_prompt         系统提示词（若不为空作为首条 system 消息）
 * @param messages              内部格式消息数组
 * @param add_reasoning_content 是否在 assistant 消息中附带 reasoning_content 字段
 * @return                      转换后的消息数组
 */
static cJSON *convert_messages_openai(const char *system_prompt, cJSON *messages, bool add_reasoning_content)
{
	cJSON *out = cJSON_CreateArray();
	/* system 提示词放在消息数组最前面，作为第一条 system 消息 */
	if (system_prompt && system_prompt[0]) {
		cJSON *sys = cJSON_CreateObject();
		cJSON_AddStringToObject(sys, "role", "system");
		cJSON_AddStringToObject(sys, "content", system_prompt);
		cJSON_AddItemToArray(out, sys);
	}

	if (!messages || !cJSON_IsArray(messages)) {
		return out;
	}

	cJSON *msg = NULL;
	cJSON_ArrayForEach(msg, messages) {
		cJSON *role = cJSON_GetObjectItem(msg, "role");
		cJSON *content = cJSON_GetObjectItem(msg, "content");
		if (!role || !cJSON_IsString(role)) {
			continue;
		}

		/* 简单文本 content：直接复制 role/content 字段 */
		if (content && cJSON_IsString(content)) {
			sanitize_utf8(content->valuestring);
			cJSON *m = cJSON_CreateObject();
			cJSON_AddStringToObject(m, "role", role->valuestring);
			cJSON_AddStringToObject(m, "content", content->valuestring);
			cJSON_AddItemToArray(out, m);
			continue;
		}

		/* 非数组 content：跳过 */
		if (!content || !cJSON_IsArray(content)) {
			continue;
		}

		/* ── assistant 消息转换：块数组 → text + tool_calls + reasoning_content ── */
		if (strcmp(role->valuestring, "assistant") == 0) {
			cJSON *m = cJSON_CreateObject();
			cJSON_AddStringToObject(m, "role", "assistant");

			char *text_buf = NULL;       /* 所有 text 块的拼接 */
			char *reasoning_buf = NULL;  /* 最后一条 reasoning 块的文本 */
			size_t off = 0;
			cJSON *block = NULL;
			cJSON *tool_calls = NULL;    /* 工具调用数组 */
			cJSON_ArrayForEach(block, content) {
				cJSON *btype = cJSON_GetObjectItem(block, "type");
				if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
					/* 拼接所有 text 块为一个字符串 */
					cJSON *text = cJSON_GetObjectItem(block, "text");
					if (text && cJSON_IsString(text)) {
						size_t tlen = strlen(text->valuestring);
						char *tmp = realloc(text_buf, off + tlen + 1);
						if (tmp) {
							text_buf = tmp;
							memcpy(text_buf + off, text->valuestring, tlen);
							off += tlen;
							text_buf[off] = '\0';
						}
					}
				} else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "reasoning") == 0) {
					/* 取 reasoning 块的文本（最后一条生效） */
					cJSON *text = cJSON_GetObjectItem(block, "text");
					if (text && cJSON_IsString(text) && text->valuestring[0]) {
						kfree(reasoning_buf);
						reasoning_buf = strdup(text->valuestring);
					}
				} else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_use") == 0) {
					/* tool_use 块 → OpenAI tool_calls 格式：
					 * {"id":"call_xxx", "type":"function", "function":{"name":"...", "arguments":"{...}"}} */
					if (!tool_calls) tool_calls = cJSON_CreateArray();
					cJSON *id = cJSON_GetObjectItem(block, "id");
					cJSON *name = cJSON_GetObjectItem(block, "name");
					cJSON *input = cJSON_GetObjectItem(block, "input");
					if (!name || !cJSON_IsString(name)) {
						continue;
					}

					cJSON *tc = cJSON_CreateObject();
					if (id && cJSON_IsString(id)) {
						cJSON_AddStringToObject(tc, "id", id->valuestring);
					}
					cJSON_AddStringToObject(tc, "type", "function");

					cJSON *func = cJSON_CreateObject();
					cJSON_AddStringToObject(func, "name", name->valuestring);
					if (input) {
						/* Anthropic 的 input 是 JSON 对象，OpenAI 需要序列化为 JSON 字符串 */
						char *args = cJSON_PrintUnformatted(input);
						if (args) {
							cJSON_AddStringToObject(func, "arguments", args);
							kfree(args);
						}
					}
					cJSON_AddItemToObject(tc, "function", func);
					cJSON_AddItemToArray(tool_calls, tc);
				}
			}

			sanitize_utf8(text_buf);
			if ((!text_buf || !text_buf[0]) && !tool_calls &&
			    !(add_reasoning_content && reasoning_buf && reasoning_buf[0])) {
				cJSON_Delete(m);
				kfree(text_buf);
				kfree(reasoning_buf);
				continue;
			}
			cJSON_AddStringToObject(m, "content", text_buf ? text_buf : "");
			if (tool_calls) {
				cJSON_AddItemToObject(m, "tool_calls", tool_calls);
			}
			/* 某些 OpenAI 兼容 API 需要 reasoning_content 字段来传递推理内容 */
			if (add_reasoning_content && reasoning_buf && reasoning_buf[0]) {
				cJSON_AddStringToObject(m, "reasoning_content", reasoning_buf);
			}
			cJSON_AddItemToArray(out, m);
			kfree(text_buf);
			kfree(reasoning_buf);
			continue;
		}

		/* ── user 消息转换：处理 text / tool_result / image_url 块 ── */
		if (strcmp(role->valuestring, "user") != 0) {
			continue;  /* 未知角色：跳过 */
		}

		cJSON *block = NULL;
		bool has_user_text = false;   /* content 中是否有 text 块 */
		bool has_user_image = false;  /* content 中是否有 image_url 块 */
		char *text_buf = NULL;
		size_t off = 0;
		cJSON *user_content = NULL;   /* 多模态 content 数组 */
		cJSON_ArrayForEach(block, content) {
			cJSON *btype = cJSON_GetObjectItem(block, "type");
			if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_result") == 0) {
				/* tool_result 块 → OpenAI tool 消息（role=tool） */
				cJSON *tool_id = cJSON_GetObjectItem(block, "tool_use_id");
				cJSON *tcontent = cJSON_GetObjectItem(block, "content");
				if (!tool_id || !cJSON_IsString(tool_id)) {
					continue;
				}
				cJSON *tm = cJSON_CreateObject();
				cJSON_AddStringToObject(tm, "role", "tool");
				cJSON_AddStringToObject(tm, "tool_call_id", tool_id->valuestring);
				if (tcontent && cJSON_IsString(tcontent)) {
					sanitize_utf8(tcontent->valuestring);
					cJSON_AddStringToObject(tm, "content", tcontent->valuestring);
				} else {
					cJSON_AddStringToObject(tm, "content", "");
				}
				cJSON_AddItemToArray(out, tm);
			} else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "image_url") == 0) {
				/* image_url 块：加入多模态 content 数组 */
				if (!user_content) {
					user_content = cJSON_CreateArray();
				}
				if (user_content) {
					cJSON *dup = cJSON_Duplicate(block, 1);
					if (dup) {
						cJSON_AddItemToArray(user_content, dup);
						has_user_image = true;
					}
				}
			} else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
				/* text 块：拼接到文本缓冲区 */
				cJSON *text = cJSON_GetObjectItem(block, "text");
				if (text && cJSON_IsString(text)) {
					size_t tlen = strlen(text->valuestring);
					char *tmp = realloc(text_buf, off + tlen + 1);
					if (tmp) {
						text_buf = tmp;
						memcpy(text_buf + off, text->valuestring, tlen);
						off += tlen;
						text_buf[off] = '\0';
					}
					has_user_text = true;
				}
			}
		}

		/* 无图片时清理已创建的空 content 数组 */
		if (!has_user_image && user_content) {
			cJSON_Delete(user_content);
			user_content = NULL;
		}

		if (has_user_image) {
			/* 多模态消息：content 为数组 [{image_url}, {text}] */
			if (has_user_text && user_content) {
				/* 文本块放在图片块之后（OpenAI 多模态消息惯例） */
				cJSON *tb = cJSON_CreateObject();
				if (tb) {
					cJSON_AddStringToObject(tb, "type", "text");
					cJSON_AddStringToObject(tb, "text", text_buf ? text_buf : "");
					cJSON_AddItemToArray(user_content, tb);
				}
			}
			if (user_content && cJSON_GetArraySize(user_content) > 0) {
				cJSON *um = cJSON_CreateObject();
				cJSON_AddStringToObject(um, "role", "user");
				cJSON_AddItemToObject(um, "content", user_content);
				cJSON_AddItemToArray(out, um);
				user_content = NULL;  /* 所有权已转移 */
			}
			if (user_content) {
				cJSON_Delete(user_content);
			}
		} else if (has_user_text) {
			/* 纯文本消息：content 为字符串 */
			sanitize_utf8(text_buf);
			cJSON *um = cJSON_CreateObject();
			cJSON_AddStringToObject(um, "role", "user");
			cJSON_AddStringToObject(um, "content", text_buf ? text_buf : "");
			cJSON_AddItemToArray(out, um);
		}
		kfree(text_buf);
	}

	return out;
}

/**
 * 构建完整的 OpenAI Chat Completions JSON 请求体。
 *
 * 顶层 JSON 结构：
 * {
 *   "model": "...",
 *   "max_tokens" | "max_completion_tokens": N,
 *   "thinking": {"type": "disabled"} | {"type": "enabled"},  // 可选
 *   "reasoning_effort": "low" | "medium" | "high",           // 可选
 *   "messages": [...],
 *   "tools": [...],
 *   "tool_choice": "auto"
 * }
 *
 * thinking 和 reasoning_effort 是 OpenAI 扩展字段，用于控制推理模型的思维链行为：
 * - thinking.type="disabled" → 完全禁止思维链
 * - thinking.type="enabled" + reasoning_effort → 允许思维链并设置推理强度
 *
 * @param system_prompt          系统提示词
 * @param messages               历史消息数组（内容块格式）
 * @param tools_json             工具定义 JSON 数组
 * @param model                  模型名称
 * @param max_completion_tokens  最大输出 token
 * @param use_max_tokens_field   使用 "max_tokens"（DeepSeek 兼容）而非 "max_completion_tokens"
 * @param disable_thinking       禁止思维链
 * @param reasoning_effort       推理强度
 * @param add_reasoning_content  是否在 assistant 消息中包含 reasoning_content
 * @return                       堆分配的 cJSON 请求体
 */
cJSON *llm_openai_build_tools_body(const char *system_prompt,
                                   cJSON *messages,
                                   const char *tools_json,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort,
                                   bool add_reasoning_content,
                                   bool response_format_json_object)
{
	cJSON *body = cJSON_CreateObject();
	if (!body) {
		return NULL;
	}

	cJSON_AddStringToObject(body, "model", model ? model : "");
	/* 根据平台选择 max_tokens 或 max_completion_tokens（DeepSeek 不支持后者） */
	cJSON_AddNumberToObject(
		body,
		use_max_tokens_field ? "max_tokens" : "max_completion_tokens",
		max_completion_tokens);
	/* 设置 thinking/reasoning 扩展字段 */
	if (disable_thinking) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "disabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
	} else if (reasoning_effort && reasoning_effort[0]) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "enabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
		cJSON_AddStringToObject(body, "reasoning_effort", reasoning_effort);
	}

	/* 转换消息格式并加入请求体 */
	cJSON_AddItemToObject(body, "messages",
	                      convert_messages_openai(system_prompt, messages, add_reasoning_content));

	/* 工具调用：转换工具定义并设置 tool_choice=auto */
	if (tools_json) {
		cJSON *tools = convert_tools_openai(tools_json);
		if (tools) {
			cJSON_AddItemToObject(body, "tools", tools);
			cJSON_AddStringToObject(body, "tool_choice", "auto");
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
 * 创建多模态 content 数组。
 *
 * 输出格式：
 * [{"type":"image_url","image_url":{"url":"data:<mime>;base64,<data>"}}, ..., {"type":"text","text":"..."}]
 *
 * @param text         文本提示（可为 NULL / 空字符串）
 * @param images       图片数组（已 base64 编码）
 * @param image_count  图片数量
 * @return             content 数组；无图片或内存不足返回 NULL
 */
cJSON *llm_create_multimodal_content(const char *text, const llm_image_content_t *images, int image_count)
{
	if (!images || image_count <= 0) {
		return NULL;
	}

	cJSON *content_array = cJSON_CreateArray();
	if (!content_array) {
		return NULL;
	}

	/* 逐个添加图片块：{"type":"image_url", "image_url":{"url":"data:mime;base64,..."}} */
	for (int i = 0; i < image_count; i++) {
		if (!images[i].image_data) {
			continue;
		}

		cJSON *image_block = cJSON_CreateObject();
		if (!image_block) {
			continue;
		}

		cJSON_AddStringToObject(image_block, "type", "image_url");
		cJSON *image_url_obj = cJSON_CreateObject();
		if (image_url_obj) {
			/* 构造 data URI：data:<mime_type>;base64,<base64_data> */
			char *url_data = kmalloc(images[i].image_data_len + 64, GFP_KERNEL);
			if (url_data) {
				snprintf(url_data,
				         images[i].image_data_len + 64,
				         "data:%s;base64,%s",
				         images[i].mime_type,
				         images[i].image_data);
				cJSON_AddStringToObject(image_url_obj, "url", url_data);
				kfree(url_data);
			}
			cJSON_AddItemToObject(image_block, "image_url", image_url_obj);
		}
		cJSON_AddItemToArray(content_array, image_block);
	}

	/* 文本块放在图片块之后（OpenAI 多模态消息惯例：视觉内容在前，指令在后） */
	if (text && text[0]) {
		cJSON *text_block = cJSON_CreateObject();
		if (text_block) {
			cJSON_AddStringToObject(text_block, "type", "text");
			cJSON_AddStringToObject(text_block, "text", text);
			cJSON_AddItemToArray(content_array, text_block);
		}
	}

	return content_array;
}

/**
 * 构建 OpenAI 视觉理解请求体。
 *
 * 消息格式：
 * messages = [
 *   {"role":"system","content":"..."},          // 无 system_prompt 时省略
 *   {"role":"user","content":[{...},{...}]}     // 多模态 content 数组
 * ]
 *
 * @param system_prompt          系统提示词（可为 NULL）
 * @param user_text              用户文本
 * @param images                 图片数组
 * @param image_count            图片数量
 * @param model                  模型名称
 * @param max_completion_tokens  最大输出 token 数
 * @param use_max_tokens_field   使用 "max_tokens" 字段
 * @param disable_thinking       禁止思维链
 * @param reasoning_effort       推理强度
 * @return                       堆分配的 cJSON 请求体；图片数据无效时返回 NULL
 */
cJSON *llm_openai_build_image_body(const char *system_prompt,
                                   const char *user_text,
                                   const llm_image_content_t *images,
                                   int image_count,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort)
{
	cJSON *body = cJSON_CreateObject();
	if (!body) {
		return NULL;
	}

	cJSON_AddStringToObject(body, "model", model ? model : "");
	cJSON_AddNumberToObject(
		body,
		use_max_tokens_field ? "max_tokens" : "max_completion_tokens",
		max_completion_tokens);
	/* thinking/reasoning 扩展字段（与工具调用请求体一致） */
	if (disable_thinking) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "disabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
	} else if (reasoning_effort && reasoning_effort[0]) {
		cJSON *thinking = cJSON_CreateObject();
		cJSON_AddStringToObject(thinking, "type", "enabled");
		cJSON_AddItemToObject(body, "thinking", thinking);
		cJSON_AddStringToObject(body, "reasoning_effort", reasoning_effort);
	}

	cJSON *messages = cJSON_CreateArray();
	/* system 消息（可选） */
	if (system_prompt && system_prompt[0]) {
		cJSON *sys_msg = cJSON_CreateObject();
		cJSON_AddStringToObject(sys_msg, "role", "system");
		cJSON_AddStringToObject(sys_msg, "content", system_prompt);
		cJSON_AddItemToArray(messages, sys_msg);
	}

	/* user 消息：多模态 content 数组 */
	cJSON *user_msg = cJSON_CreateObject();
	cJSON_AddStringToObject(user_msg, "role", "user");
	cJSON *content = llm_create_multimodal_content(user_text, images, image_count);
	if (!content) {
		/* 图片数据无效：回退失败 */
		cJSON_Delete(user_msg);
		cJSON_Delete(messages);
		cJSON_Delete(body);
		return NULL;
	}
	cJSON_AddItemToObject(user_msg, "content", content);
	cJSON_AddItemToArray(messages, user_msg);
	cJSON_AddItemToObject(body, "messages", messages);
	return body;
}
#endif

/**
 * 解析 OpenAI Chat Completions 响应 JSON，提取到 llm_response_t 结构体。
 *
 * 解析目标 JSON 结构：
 * {
 *   "choices": [{
 *     "finish_reason": "stop" | "tool_calls",
 *     "message": {
 *       "content": "...",
 *       "reasoning_content": "...",                                       // 可选
 *       "tool_calls": [{"id":"...", "function":{"name":"...", "arguments":"{...}"}}, ...]  // 可选
 *     }
 *   }]
 * }
 *
 * resp->tool_use 的判断依据：finish_reason == "tool_calls" 或 tool_calls 数组非空
 *
 * @param json_text  API 响应 JSON 原文
 * @param resp       输出响应结构体（重置后填充，堆内存由调用者通过 llm_response_free 释放）
 * @return           成功返回 0
 */
err_t llm_openai_parse_response(const char *json_text, llm_response_t *resp)
{
	if (!json_text || !resp) {
		return ERR_INVALID_ARG;
	}

	cJSON *root = cJSON_Parse(json_text);
	if (!root) {
		return ERR_FAIL;
	}

	/* 解析路径：root → choices[0] → message */
	cJSON *choices = cJSON_GetObjectItem(root, "choices");
	cJSON *choice0 = choices && cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
	if (choice0) {
		/* finish_reason 判断：tool_calls 表示模型请求调用工具 */
		cJSON *finish = cJSON_GetObjectItem(choice0, "finish_reason");
		if (finish && cJSON_IsString(finish)) {
			resp->tool_use = strcmp(finish->valuestring, "tool_calls") == 0;
		}

		cJSON *message = cJSON_GetObjectItem(choice0, "message");
		if (message) {
			/* 提取文本内容 */
			cJSON *content = cJSON_GetObjectItem(message, "content");
			if (content && cJSON_IsString(content)) {
				size_t tlen = strlen(content->valuestring);
				resp->text = kzalloc(tlen + 1, GFP_KERNEL);
				if (resp->text) {
					memcpy(resp->text, content->valuestring, tlen);
					resp->text_len = tlen;
				}
			}

			/* 提取 reasoning_content（仅当非空时分配） */
			cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
			if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
				size_t rlen = strlen(reasoning->valuestring);
				resp->reasoning_content = kzalloc(rlen + 1, GFP_KERNEL);
				if (resp->reasoning_content) {
					memcpy(resp->reasoning_content, reasoning->valuestring, rlen);
					resp->reasoning_content_len = rlen;
				}
			}

			/* 提取 tool_calls 数组：
			 * 每个 tool_call 格式：{"id":"call_xxx", "type":"function", "function":{"name":"...", "arguments":"{...}"}} */
			cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
			if (tool_calls && cJSON_IsArray(tool_calls)) {
				cJSON *tc = NULL;
				cJSON_ArrayForEach(tc, tool_calls) {
					if (resp->call_count >= MAX_TOOL_CALLS) {
						break;  /* 超过最大工具调用数限制 */
					}

					llm_tool_call_t *call = &resp->calls[resp->call_count];
					cJSON *id = cJSON_GetObjectItem(tc, "id");
					cJSON *func = cJSON_GetObjectItem(tc, "function");
					if (id && cJSON_IsString(id)) {
						strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
					}
					if (func) {
						cJSON *name = cJSON_GetObjectItem(func, "name");
						cJSON *args = cJSON_GetObjectItem(func, "arguments");
						if (name && cJSON_IsString(name)) {
							strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
						}
						if (args && cJSON_IsString(args)) {
							/* arguments 是 JSON 字符串（function calling 标准格式） */
							call->input = strdup(args->valuestring);
							if (call->input) {
								call->input_len = strlen(call->input);
							}
						}
					}
					log_tool_call_parse("openai",
					                    resp->call_count,
					                    call->id,
					                    call->name,
					                    call->input);
					resp->call_count++;
				}
				/* 只要有工具调用就标记 tool_use（即使 finish_reason 不匹配） */
				if (resp->call_count > 0) {
					resp->tool_use = true;
				}
			}
		}
	}

	cJSON_Delete(root);
	return 0;
}
