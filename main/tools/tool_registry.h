/* 工具注册表接口。 */

#pragma once

#include "daima_err.h"
#include <stddef.h>

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* 输入参数的 JSON Schema 字符串 */
    daima_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} daima_tool_t;

/**
 * 初始化工具注册表并注册所有内置工具。
 */
daima_err_t tool_registry_init(void);

/**
 * 获取用于 API 请求的预构建工具数组 JSON 字符串。
 * 若未注册任何工具则返回 NULL。
 */
const char *tool_registry_get_tools_json(void);

/**
 * 按名称执行工具。
 *
 * @param name         工具名称（如 "weather"）
 * @param input_json   工具输入的 JSON 字符串
 * @param output       工具结果文本的输出缓冲区
 * @param output_size  输出缓冲区大小
 * @return 成功返回 DAIMA_OK，工具不存在返回 DAIMA_ERR_NOT_FOUND
 */
daima_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);
