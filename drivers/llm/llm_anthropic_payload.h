/* Anthropic Messages API 请求构造/响应解析接口。
 * - 负责构建符合 Anthropic /v1/messages 协议的 JSON 请求体
 * - 与 OpenAI 协议的关键差异：system 是请求顶层字段、tool_choice 是对象、content 是块数组
 * - 支持工具调用（tool_use/tool_result 块）和推理（thinking/reasoning 块）
 */

#pragma once

#include <stdbool.h>

#include "drivers/llm/llm_proxy.h"

/**
 * 构建 Anthropic Messages 工具调用请求体。
 *
 * 构造的 JSON 结构：
 * {
 *   "model": "...",
 *   "max_tokens": N,
 *   "system": "系统提示词",                              // 顶层字符串，非 messages 数组内
 *   "messages": [{"role": "user"|"assistant", "content": [{...}, ...]}, ...],
 *   "tools": [{"name":"...", "description":"...", "input_schema":{...}}, ...],
 *   "tool_choice": {"type": "auto"},
 *   "thinking": {"type": "disabled"} | {"type": "enabled"},
 *   "output_config": {"effort": "low"|"medium"|"high"}
 * }
 *
 * @param system_prompt     系统提示词，不为空时作为顶层 "system" 字段
 * @param messages           历史消息数组（含 tool_result / thinking / tool_use 块）
 * @param tools_json         工具定义 JSON 数组字符串；NULL 表示不使用工具
 * @param model              模型名称（如 "claude-sonnet-4-20250514"）
 * @param max_tokens         最大输出 token 数（Anthropic 仅支持 max_tokens）
 * @param disable_thinking   禁止思维链（thinking type=disabled）
 * @param reasoning_effort   推理强度；非空时设置 output_config.effort
 * @return                   堆分配的 cJSON 对象，调用者负责释放
 */
cJSON *llm_anthropic_build_tools_body(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort);

#ifdef ENABLE_VISION
/**
 * 构建 Anthropic Messages 视觉理解请求体。
 *
 * 当前实现为存根（stub）：仅设置纯文本 user 消息，图片参数被忽略。
 * 完整的 Anthropic 图片支持需将 content 构建为 [{type:"image", source:{type:"base64", media_type:"...", data:"..."}}, {type:"text", text:"..."}]。
 *
 * @param system_prompt     系统提示词（设为顶层 "system" 字段）
 * @param user_text         用户文本提示
 * @param images            图片内容数组（当前未使用）
 * @param image_count       图片数量（当前未使用）
 * @param model             模型名称
 * @param max_tokens        最大输出 token 数
 * @param disable_thinking  禁止思维链
 * @param reasoning_effort  推理强度
 * @return                  堆分配的 cJSON 对象，调用者负责释放
 */
cJSON *llm_anthropic_build_image_body(const char *system_prompt,
                                      const char *user_text,
                                      const llm_image_content_t *images,
                                      int image_count,
                                      const char *model,
                                      int max_tokens,
                                      bool disable_thinking,
                                      const char *reasoning_effort);
#endif

/**
 * 解析 Anthropic Messages 响应 JSON。
 *
 * 提取路径：
 * - resp->text：content[] 中所有 text 块拼接
 * - resp->reasoning_content：content[] 中第一个 thinking/reasoning 块的文本
 * - resp->calls[]：content[] 中 tool_use 块的 id / name / input
 * - resp->tool_use：stop_reason == "tool_use"
 *
 * 与 OpenAI 解析的关键差异：
 * - content 是顶层数组（非 choices[0].message.content）
 * - tool_use 块直接使用 input 对象（非 function.arguments 字符串）
 * - stop_reason 替代 finish_reason
 *
 * @param json_text  响应 JSON 原文
 * @param resp       输出响应结构体（堆内存由调用者通过 llm_response_free 释放）
 * @return           成功返回 0，解析失败返回错误码
 */
err_t llm_anthropic_parse_response(const char *json_text, llm_response_t *resp);
