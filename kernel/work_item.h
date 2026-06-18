/* 工作项 JSONL 存储接口。
 * 提供工作项（work item）的增删改查操作，持久化到 JSONL 文件。
 * 工作项用于记录 agent 执行过程中的关键事件：
 * 工具失败、代码修改、文件创建等，支持后续审计和统计。 */

#pragma once

#include "cjson.h"
#include "err.h"

#include <stdbool.h>
#include <stddef.h>

/* 工作项列表：从文件加载的数据集合 */
typedef struct {
	cJSON *items;		/* 工作项 JSON 数组 */
	int invalid_lines;	/* 解析失败的行数（数据损坏指示） */
} work_item_list_t;

/* 字段值合法性校验 */
bool work_item_type_valid(const char *value);
bool work_item_source_valid(const char *value);
bool work_item_status_valid(const char *value);
bool work_item_priority_valid(const char *value);

/* 释放工作项列表内存 */
void work_item_list_free(work_item_list_t *list);

/* 从存储加载所有工作项 */
err_t work_item_store_load(work_item_list_t *out);

/* 新增工作项（自动分配 ID 和时间戳） */
err_t work_item_store_add(const cJSON *input, cJSON **out_item);

/* 按 ID 更新工作项 */
err_t work_item_store_update(const char *id, const cJSON *input, cJSON **out_item);

/* 批量更新工作项状态 */
err_t work_item_store_batch_update(const cJSON *ids, const char *status, int *out_count);

/* 收集工作项（从工具执行中提取关键信息并存储） */
err_t work_item_store_collect(const char *type, const char *source, const char *title, const char *description);

/* 结构化收集工作项（输入完整 JSON） */
err_t work_item_store_collect_structured(const cJSON *input, cJSON **out_item);
