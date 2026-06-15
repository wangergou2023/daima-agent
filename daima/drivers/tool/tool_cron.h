/* cron 工具接口。 */

#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

/**
 * 统一定时任务工具。
 * 输入 JSON：{ action:"add", name, schedule_type, interval_s?, at_epoch?, message, channel?, chat_id? }
 *          { action:"list" }
 *          { action:"remove", job_id }
 */
daima_err_t tool_cron_execute(const char *input_json, char *output, size_t output_size);
const struct tool *tool_cron_definition(void);
