/* OpenAI Chat Completions API 请求构造/响应解析接口。
 * - 负责构建符合 OpenAI /v1/chat/completions 协议的 JSON 请求体
 * - 支持工具调用（function calling）和多模态视觉输入
 * - 兼容 thinking/reasoning 扩展字段（如 reasoning_effort、reasoning_content）
 */

#pragma once

#include <stdbool.h>

#include "drivers/llm/llm_proxy.h"

/**
 * 构建 OpenAI 工具调用请求体。
 *
 * 构造的 JSON 结构：
 * {
 *   "model": "...",
 *   "max_tokens" | "max_completion_tokens": N,
 *   "thinking": {"type": "disabled"} | {"type": "enabled"},
 *   "reasoning_effort": "low" | "medium" | "high",
 *   "messages": [{"role": "system"|"user"|"assistant"|"tool", "content": "..."|[...]}, ...],
 *   "tools": [{"type": "function", "function": {"name": "...", "description": "...", "parameters": {...}}}, ...],
 *   "tool_choice": "auto"
 * }
 *
 * @param system_prompt          系统提示词，不为空时作为首条 system 消息
 * @param messages               历史消息数组（含 tool_result / reasoning / image_url 等块）
 * @param tools_json             工具定义 JSON 数组字符串；NULL 表示不使用工具
 * @param model                  模型名称（如 "gpt-4o-mini"）
 * @param max_completion_tokens  最大输出 token 数
 * @param use_max_tokens_field   使用 "max_tokens" 字段（DeepSeek 兼容）而非 "max_completion_tokens"
 * @param disable_thinking       禁止思维链（thinking type=disabled）
 * @param reasoning_effort       推理强度（low/medium/high）；NULL 表示不设置
 * @param add_reasoning_content  是否在 assistant 消息中保留 reasoning_content 字段
 * @return                       堆分配的 cJSON 对象，调用者负责释放
 */
cJSON *llm_openai_build_tools_body(const char *system_prompt,
                                   cJSON *messages,
                                   const char *tools_json,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort,
                                   bool add_reasoning_content);

#ifdef ENABLE_VISION
/**
 * 构建 OpenAI 视觉理解请求体。
 *
 * 构造 user 消息的 content 为数组：[{"type": "image_url", "image_url": {"url": "data:mime;base64,..."}}, {"type": "text", "text": "..."}]
 * 无 system prompt 时省略 system 消息。
 *
 * @param system_prompt          系统提示词（可为 NULL）
 * @param user_text              用户文本提示
 * @param images                 图片内容数组（已 base64 编码）
 * @param image_count            图片数量
 * @param model                  模型名称
 * @param max_completion_tokens  最大输出 token 数
 * @param use_max_tokens_field   使用 "max_tokens" 字段
 * @param disable_thinking       禁止思维链
 * @param reasoning_effort       推理强度
 * @return                       堆分配的 cJSON 对象，调用者负责释放
 */
cJSON *llm_openai_build_image_body(const char *system_prompt,
                                   const char *user_text,
                                   const llm_image_content_t *images,
                                   int image_count,
                                   const char *model,
                                   int max_completion_tokens,
                                   bool use_max_tokens_field,
                                   bool disable_thinking,
                                   const char *reasoning_effort);
#endif

/**
 * 解析 OpenAI Chat Completions 响应 JSON。
 *
 * 提取路径：
 * - resp->text：choices[0].message.content
 * - resp->reasoning_content：choices[0].message.reasoning_content
 * - resp->calls[]：choices[0].message.tool_calls[].id / function.name / function.arguments
 * - resp->tool_use：finish_reason == "tool_calls"
 *
 * @param json_text  响应 JSON 原文
 * @param resp       输出响应结构体（堆内存由调用者通过 llm_response_free 释放）
 * @return           成功返回 0，解析失败返回错误码
 */
err_t llm_openai_parse_response(const char *json_text, llm_response_t *resp);
