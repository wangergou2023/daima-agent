/* Agent 行为规则注入接口。
 * 从 SPIFFS 配置中加载规则文本并注入到 system prompt，
 * 用于在运行时动态调整 agent 行为（如禁止某些工具、强制特定输出格式等）。 */

#pragma once

#include "err.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * 加载规则文本并写入缓冲区（追加到现有内容末尾）。
 * 从 spiffs_data/config/rules.txt 或等效路径读取。
 * @param buffer      输出缓冲区（已有内容不会被清除）
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，文件不存在时也返回 0
 */
err_t rules_injection_load(char *buffer, size_t buffer_size);
