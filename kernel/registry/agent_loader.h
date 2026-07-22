/* 预定义 Agent 加载器：从 spiffs_data/agents/ 目录加载 JSON 定义
 * 并注册到 Agent Registry（跳过已存在的 agent_id）。 */
#pragma once

#include "err.h"

/**
 * 扫描 spiffs_data/agents/ 目录，将每个子目录下的 agent.json
 * 注册到 Agent Registry。已存在的 agent_id 会被跳过（不覆盖）。
 * 应在 agent_registry_init() 之后调用。
 *
 * @return 0 成功，非 0 表示扫描目录失败（非致命）
 */
err_t agent_loader_seed_from_spiffs(void);
