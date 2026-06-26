/* 大模型代理接口。
 * - 提供统一的 LLM 调用封装（支持工具调用/多模态）
 * - Host/MIPS 共用该头文件，具体实现由平台文件提供
 */

#pragma once

#include "err.h"
#include "cjson.h"
#include <stddef.h>
#include <stdbool.h>

#include "autoconf.h"

/**
 * 初始化大模型代理。
 * - 读取 runtime_config 中当前生效的 provider 配置
 * - 组装 OpenAI 兼容请求 URL
 */
err_t llm_proxy_init(void);

/**
 * 设置大模型 API Key（仅进程内生效）。
 */
err_t llm_set_api_key(const char *api_key);

/**
 * 设置模型标识（如 "kimi-k2.6" / "gpt-4o-mini"）。
 */
err_t llm_set_model(const char *model);

/**
 * 获取当前生效的模型名（进程内缓存）。
 */
const char *llm_get_model_name(void);

/**
 * 获取当前模型的上下文窗口估计值。
 * 优先使用 config.json 中当前 provider/common 的 context_limit_tokens，
 * 否则回退到默认值。
 */
int llm_get_context_limit_tokens(void);

/* ── 工具调用支持 ──────────────────────────────────────────── */

typedef struct {
    char id[64];        /* 工具调用 ID，如 "toolu_xxx" */
    char name[32];      /* 工具名称，如 "weather" */
    char *input;        /* 堆分配的 JSON 字符串（调用者需释放） */
    size_t input_len;   /* input 字符串长度 */
} llm_tool_call_t;

typedef struct {
    char *text;                                  /* 累计的文本块（堆内存） */
    size_t text_len;                             /* 文本长度 */
    char *reasoning_content;                     /* 兼容部分推理模型的 reasoning_content */
    size_t reasoning_content_len;                /* reasoning_content 长度 */
    llm_tool_call_t calls[MAX_TOOL_CALLS];  /* 工具调用列表 */
    int call_count;                              /* 工具调用数量 */
    bool tool_use;                               /* stop_reason == "tool_use" */
} llm_response_t;

typedef struct {
    const char *api_key;
    const char *model;
    const char *base_url;
    const char *api_url;
    const char *api_mode;
    const char *thinking_mode;
    const char *reasoning_effort;
    int max_output_tokens;
    int request_timeout_ms;
    bool use_anthropic_api;
    bool add_reasoning_content;
    bool use_max_tokens_field;
} llm_request_options_t;

/**
 * 释放 llm_response_t 内部的堆内存
 */
void llm_response_free(llm_response_t *resp);

/* ── 图片理解支持 ──────────────────────────────────────────── */

#ifdef ENABLE_VISION

/**
 * 图片内容块（用于多模态消息）
 * - image_data 为 base64 编码，不包含 data: 前缀
 */
typedef struct {
    char *image_data;       /* base64 编码的图片数据 */
    size_t image_data_len;  /* base64 数据长度 */
    char mime_type[32];     /* 如 "image/png", "image/jpeg" */
} llm_image_content_t;

/**
 * 读取图片文件并编码为 base64
 * @param image_path 图片文件路径
 * @param out_content 输出图片内容块（需调用 llm_image_content_free 释放）
 * @return 成功返回 0
 */
err_t llm_image_read_file(const char *image_path, llm_image_content_t *out_content);

/**
 * 释放图片内容块内存
 */
void llm_image_content_free(llm_image_content_t *content);

/**
 * 创建包含图片和文本的多模态消息
 * @param text 文本提示（可为 NULL）
 * @param images 图片内容数组
 * @param image_count 图片数量
 * @return cJSON 数组（包含 image_url 和 text 块），失败返回 NULL
 */
cJSON *llm_create_multimodal_content(const char *text, const llm_image_content_t *images, int image_count);

/**
 * 发送带图片的对话请求（OpenAI 兼容格式）
 * @param system_prompt 系统提示
 * @param user_text 用户文本提示
 * @param images 图片数组
 * @param image_count 图片数量
 * @param resp 输出响应
 * @return 成功返回 0
 */
err_t llm_chat_with_images(const char *system_prompt,
                                const char *user_text,
                                const llm_image_content_t *images,
                                int image_count,
                                llm_response_t *resp);

#endif /* ENABLE_VISION */

/**
 * 向配置的大模型 API 发送带工具的对话补全请求（非流式）。
 *
 * @param system_prompt  系统提示字符串
 * @param messages       消息 cJSON 数组（由调用方持有）
 * @param tools_json     预构建的工具数组 JSON 字符串；NULL 表示不使用工具
 * @param resp           输出：包含文本与工具调用的结构化响应
 * @return 成功返回 0
 */
err_t llm_chat_tools(const char *system_prompt,
                          cJSON *messages,
                          const char *tools_json,
                          llm_response_t *resp);

err_t llm_chat_tools_with_model(const char *system_prompt,
                                      cJSON *messages,
                                      const char *tools_json,
                                      const char *model_override,
                                      llm_response_t *resp);

err_t llm_chat_tools_with_model_and_format(const char *system_prompt,
                                           cJSON *messages,
                                           const char *tools_json,
                                           const char *model_override,
                                           bool response_format_json_object,
                                           llm_response_t *resp);

err_t llm_chat_tools_with_provider_and_format(const char *system_prompt,
                                              cJSON *messages,
                                              const char *tools_json,
                                              const char *provider_name,
                                              const char *model_override,
                                              bool response_format_json_object,
                                              llm_response_t *resp);

/* ── 异步 LLM 调用（非阻塞） ──────────────────────────────── */

typedef struct llm_async_chat llm_async_chat_t;

// 发起异步 LLM 调用，参数和 llm_chat_tools 一致但不阻塞
llm_async_chat_t *llm_chat_tools_async(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const char *model_override);

llm_async_chat_t *llm_chat_tools_async_with_format(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const char *model_override,
    bool response_format_json_object);

llm_async_chat_t *llm_chat_tools_async_with_provider_and_format(
    const char *system_prompt,
    cJSON *messages,
    const char *tools_json,
    const char *provider_name,
    const char *model_override,
    bool response_format_json_object);

// 检查 LLM 调用是否完成（非阻塞，0开销）
bool llm_chat_async_is_done(llm_async_chat_t *chat);

// 获取结果（已完成时立即返回，未完成时阻塞等待）
err_t llm_chat_async_get_response(
    llm_async_chat_t *chat, llm_response_t *resp);

// 释放资源（如果请求还在进行中会 cancel）
void llm_chat_async_free(llm_async_chat_t *chat);
