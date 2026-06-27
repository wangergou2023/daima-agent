/* LLM 模型回退配置与调用接口。
 * - 当主模型调用失败时，按配置列表依次尝试备用模型
 * - 配置来源：category_routing.json → fallback_models.json → config.json → 运行时默认
 * - 支持环境变量 MODEL_FALLBACK_ENABLED=0 强制禁用
 */

#pragma once

#include "err.h"
#include "drivers/llm/llm_proxy.h"
#include "cjson.h"

#include <stdbool.h>

/* 回退链最大 provider 数量 */
#define FALLBACK_MAX_MODELS 5

/**
 * Provider 回退配置。
 * providers[] 为首选 provider（索引 0）及其备用 provider 列表。
 */
typedef struct {
	bool enabled;                           /* 是否启用回退功能 */
	char providers[FALLBACK_MAX_MODELS][64]; /* provider 名称列表（首条为主，后续为备用） */
	int provider_count;                     /* 有效 provider 数量 */
} model_fallback_cfg_t;

/**
 * 加载模型回退配置。
 *
 * 优先级：
 * 1. 环境变量 MODEL_FALLBACK_ENABLED=0 → 直接返回 disabled
 * 2. category_routing.json 中的 model_fallback 段 → model_fallback.fallback_models
 *    或 model_fallback.fallback_providers
 * 3. fallback_models.json → 解析模型数组并映射回 provider
 * 4. config.json 中的 providers → 取非 active_provider 的其他 provider
 * 5. 默认：仅使用当前 active provider
 *
 * @return 加载到的配置（结构体值拷贝，无堆内存）
 */
model_fallback_cfg_t model_fallback_load_cfg(void);

/**
 * 带模型回退的工具调用请求。
 *
 * 执行流程：
 * 1. 用当前主模型发起 llm_chat_tools
 * 2. 成功 → 恢复主模型并返回
 * 3. 失败 → 加载回退配置，依次尝试备用模型
 * 4. 仍全部失败 → 恢复主模型，返回最后一次错误
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组
 * @param tools_json     工具定义 JSON（可为 NULL）
 * @param resp           输出响应（调用者通过 llm_response_free 释放堆内存）
 * @return               成功返回 0
 */
err_t model_fallback_chat_with_fallback(
	const char *system_prompt,
	cJSON *messages,
	const char *tools_json,
	const char *model_override,
	bool response_format_json_object,
	llm_response_t *resp);
