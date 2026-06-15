/* 工具注册表接口。 */

#pragma once

#include "err.h"
#include <stddef.h>

struct tool {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* 输入参数的 JSON Schema 字符串 */
    err_t (*execute)(const char *input_json, char *output, size_t output_size);
};

#define TOOL_REGISTRY_MAX_DYNAMIC 32

/**
 * 初始化工具注册表并注册所有内置工具。
 */
err_t tool_registry_init(void);

err_t tool_registry_register_dynamic(const struct tool *tool);
err_t tool_registry_unregister_dynamic(const char *tool_name);

/**
 * 获取用于 API 请求的预构建工具数组 JSON 字符串。
 * 若未注册任何工具则返回 NULL。
 */
const char *tool_registry_get_tools_json(void);

/**
 * 获取当前通道可见的工具数组 JSON。
 * PC/WebSocket 等普通通道不暴露机器人控制工具；Vector/voice 通道暴露机器人工具。
 */
const char *tool_registry_get_tools_json_for_channel(const char *channel);

/**
 * 按名称执行工具。
 *
 * @param name         工具名称（如 "weather"）
 * @param input_json   工具输入的 JSON 字符串
 * @param output       工具结果文本的输出缓冲区
 * @param output_size  输出缓冲区大小
 * @return 成功返回 DAIMA_OK，工具不存在返回 DAIMA_ERR_NOT_FOUND
 */
err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);

/**
 * 按当前消息通道执行工具。该接口用于 LLM 工具调用路径，除工具列表过滤外，
 * 再做一次执行层权限校验。
 */
err_t tool_registry_execute_for_channel(const char *channel,
                                             const char *name,
                                             const char *input_json,
                                             char *output,
                                             size_t output_size);
