/* cron 工具接口。 */

#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"
#include <stddef.h>

/**
 * 添加定时任务。
 * 输入 JSON：{ name, schedule_type ("every"/"at"), interval_s, at_epoch, message, channel?, chat_id? }
 */
daima_err_t tool_cron_add_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_cron_add_definition(void);

/**
 * 列出全部定时任务。
 * 输入 JSON：{}（无必填字段）
 */
daima_err_t tool_cron_list_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_cron_list_definition(void);

/**
 * 按 ID 删除定时任务。
 * 输入 JSON：{ job_id }
 */
daima_err_t tool_cron_remove_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_cron_remove_definition(void);
