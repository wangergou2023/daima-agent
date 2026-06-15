#include "drivers/tool/tool_work_item.h"

#include "work_item.h"

#include <stdio.h>
#include <string.h>

static const struct tool s_work_item_tool = {
    .name = "work_item",
    .description = "收集和管理结构化 work item。用于记录 bug、功能缺失、改进、技术债、文档缺口和测试缺口。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"add / list / update / review / summary，默认 list\"},"
        "\"id\":{\"type\":\"string\",\"description\":\"update 时使用的 work item ID\"},"
        "\"type\":{\"type\":\"string\",\"description\":\"defect / missing / improvement / tech_debt / docs / test_gap\"},"
        "\"source\":{\"type\":\"string\",\"description\":\"user / log / test / github_issue / heartbeat / review\"},"
        "\"title\":{\"type\":\"string\",\"description\":\"事项标题，add 时必填\"},"
        "\"description\":{\"type\":\"string\",\"description\":\"背景、上下文和影响\"},"
        "\"expected\":{\"type\":\"string\",\"description\":\"期望结果\"},"
        "\"actual\":{\"type\":\"string\",\"description\":\"当前现象或缺口\"},"
        "\"evidence\":{\"type\":\"object\",\"description\":\"{session_id, issue_url, logs, files, commands}\"},"
        "\"status\":{\"type\":\"string\",\"description\":\"new / triaged / needs_info / accepted / planned / fixing / done / rejected\"},"
        "\"priority\":{\"type\":\"string\",\"description\":\"P0 / P1 / P2 / P3\"},"
        "\"limit\":{\"type\":\"integer\",\"description\":\"list/summary 最多展示条数，默认 20\"},"
        "\"ids\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"review 批量操作时使用的 work item ID 数组\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_work_item_execute,
};

static const char *item_str(cJSON *item, const char *key)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItem(item, key));
    return value ? value : "";
}

static bool matches_filter(cJSON *item, const char *key, const char *expected)
{
    if (!expected || !expected[0]) return true;
    return strcmp(item_str(item, key), expected) == 0;
}

static void render_item_line(char *output, size_t output_size, size_t *off, cJSON *item)
{
    if (!output || !off || *off >= output_size) return;
    *off += snprintf(output + *off, output_size - *off,
                     "- %s [%s %s %s] %s\n",
                     item_str(item, "id"),
                     item_str(item, "priority"),
                     item_str(item, "type"),
                     item_str(item, "status"),
                     item_str(item, "title"));
}

static int input_limit(cJSON *input)
{
    cJSON *limit = cJSON_GetObjectItem(input, "limit");
    if (!limit || !cJSON_IsNumber(limit) || limit->valueint <= 0) return 20;
    if (limit->valueint > 200) return 200;
    return limit->valueint;
}

static err_t render_list(cJSON *input, char *output, size_t output_size)
{
    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(input, "status"));
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(input, "type"));
    const char *priority = cJSON_GetStringValue(cJSON_GetObjectItem(input, "priority"));
    int limit = input_limit(input);
    int total = cJSON_GetArraySize(list.items);
    int shown = 0;
    size_t off = snprintf(output, output_size, "Work Items (%d records", total);
    if (list.invalid_lines > 0) {
        off += snprintf(output + off, output_size - off, ", %d invalid lines skipped", list.invalid_lines);
    }
    off += snprintf(output + off, output_size - off, ")\n");

    for (int i = total - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(list.items, i);
        if (!matches_filter(item, "status", status) ||
            !matches_filter(item, "type", type) ||
            !matches_filter(item, "priority", priority)) {
            continue;
        }
        render_item_line(output, output_size, &off, item);
        if (++shown >= limit) break;
    }
    if (shown == 0) {
        snprintf(output + off, output_size - off, "（没有匹配的 work item）\n");
    }
    work_item_list_free(&list);
    return 0;
}

static bool is_high_priority(cJSON *item)
{
    const char *priority = item_str(item, "priority");
    return strcmp(priority, "P0") == 0 || strcmp(priority, "P1") == 0;
}

static bool is_implementable(cJSON *item)
{
    const char *status = item_str(item, "status");
    return strcmp(status, "accepted") == 0 || strcmp(status, "planned") == 0;
}

static void render_summary_group(char *output,
                                 size_t output_size,
                                 size_t *off,
                                 const char *title,
                                 cJSON *items,
                                 bool (*predicate)(cJSON *),
                                 int limit)
{
    int shown = 0;
    *off += snprintf(output + *off, output_size - *off, "\n%s\n", title);
    int total = cJSON_GetArraySize(items);
    for (int i = total - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(items, i);
        if (predicate && !predicate(item)) continue;
        render_item_line(output, output_size, off, item);
        if (++shown >= limit) break;
    }
    if (shown == 0) {
        *off += snprintf(output + *off, output_size - *off, "- 无\n");
    }
}

static bool is_needs_info(cJSON *item)
{
    return strcmp(item_str(item, "status"), "needs_info") == 0;
}

static bool always_true(cJSON *item)
{
    (void)item;
    return true;
}

static err_t render_summary(cJSON *input, char *output, size_t output_size)
{
    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

    int limit = input_limit(input);
    int total = cJSON_GetArraySize(list.items);
    size_t off = snprintf(output, output_size, "Work Item 摘要：%d 条记录", total);
    if (list.invalid_lines > 0) {
        off += snprintf(output + off, output_size - off, "，跳过 %d 条无效记录", list.invalid_lines);
    }
    off += snprintf(output + off, output_size - off, "\n");

    render_summary_group(output, output_size, &off, "新增事项", list.items, always_true, limit);
    render_summary_group(output, output_size, &off, "高优先级事项", list.items, is_high_priority, limit);
    render_summary_group(output, output_size, &off, "缺信息事项", list.items, is_needs_info, limit);
    render_summary_group(output, output_size, &off, "可进入实现事项", list.items, is_implementable, limit);

    work_item_list_free(&list);
    return 0;
}

static err_t render_changed_item(const char *prefix, cJSON *item, char *output, size_t output_size)
{
    if (!item) return ERR_NO_MEM;
    snprintf(output, output_size, "%s：%s [%s %s %s] %s",
             prefix,
             item_str(item, "id"),
             item_str(item, "priority"),
             item_str(item, "type"),
             item_str(item, "status"),
             item_str(item, "title"));
    return 0;
}

static bool is_reviewable(cJSON *item)
{
    const char *status = item_str(item, "status");
    return strcmp(status, "new") == 0 ||
           strcmp(status, "triaged") == 0 ||
           strcmp(status, "needs_info") == 0;
}

static err_t render_review_queue(char *output, size_t output_size)
{
    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

    int total = cJSON_GetArraySize(list.items);
    int reviewable = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, list.items) {
        if (is_reviewable(item)) reviewable++;
    }

    size_t off = snprintf(output, output_size, "待审核 Work Items (%d 条):\n", reviewable);

    for (int i = total - 1; i >= 0; i--) {
        item = cJSON_GetArrayItem(list.items, i);
        if (!is_reviewable(item)) continue;
        const char *dup_of = cJSON_GetStringValue(cJSON_GetObjectItem(item, "duplicate_of"));
        if (dup_of && dup_of[0]) {
            off += snprintf(output + off, output_size - off,
                            "- %s [%s %s %s] %s  ⚠ 疑似与 %s 重复\n",
                            item_str(item, "id"),
                            item_str(item, "priority"),
                            item_str(item, "type"),
                            item_str(item, "status"),
                            item_str(item, "title"),
                            dup_of);
        } else {
            render_item_line(output, output_size, &off, item);
        }
    }

    off += snprintf(output + off, output_size - off,
                    "\n操作：{\"action\":\"review\",\"id\":\"WI-xxx\",\"status\":\"accepted|rejected|needs_info|planned\"}\n"
                    "批量：{\"action\":\"review\",\"ids\":[\"WI-xxx\",\"WI-yyy\"],\"status\":\"...\"}\n");

    work_item_list_free(&list);
    return 0;
}

err_t tool_work_item_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *input = cJSON_Parse(input_json ? input_json : "{}");
    if (!input || !cJSON_IsObject(input)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(input);
        return ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(input, "action"));
    if (!action || !action[0]) action = "list";

    err_t err = 0;
    if (strcmp(action, "add") == 0) {
        cJSON *item = NULL;
        err = work_item_store_add(input, &item);
        if (err == 0) {
            const char *dup_of = cJSON_GetStringValue(cJSON_GetObjectItem(item, "duplicate_of"));
            if (dup_of && dup_of[0]) {
                render_changed_item("已创建 work item（疑似重复，见 description）", item, output, output_size);
            } else {
                render_changed_item("已创建 work item", item, output, output_size);
            }
        } else if (err == ERR_INVALID_ARG) {
            snprintf(output, output_size, "错误：add 需要有效 title、type/source/status/priority 枚举值");
        } else {
            snprintf(output, output_size, "错误：创建 work item 失败");
        }
        cJSON_Delete(item);
    } else if (strcmp(action, "update") == 0) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(input, "id"));
        cJSON *item = NULL;
        err = work_item_store_update(id, input, &item);
        if (err == 0) {
            render_changed_item("已更新 work item", item, output, output_size);
        } else if (err == ERR_NOT_FOUND) {
            snprintf(output, output_size, "错误：未找到 id=%s 的 work item", id ? id : "");
        } else {
            snprintf(output, output_size, "错误：update 需要有效 id 和字段值");
        }
        cJSON_Delete(item);
    } else if (strcmp(action, "list") == 0) {
        err = render_list(input, output, output_size);
    } else if (strcmp(action, "review") == 0) {
        const char *review_status = cJSON_GetStringValue(cJSON_GetObjectItem(input, "status"));
        cJSON *ids_json = cJSON_GetObjectItem(input, "ids");
        const char *single_id = cJSON_GetStringValue(cJSON_GetObjectItem(input, "id"));

        if (review_status && (ids_json || single_id)) {
            cJSON *id_array = NULL;
            if (ids_json && cJSON_IsArray(ids_json)) {
                id_array = ids_json;
            } else if (single_id) {
                id_array = cJSON_CreateArray();
                cJSON_AddItemToArray(id_array, cJSON_CreateString(single_id));
            }
            int updated = 0;
            err = work_item_store_batch_update(id_array, review_status, &updated);
            if (!ids_json) cJSON_Delete(id_array);
            if (err == 0) {
                snprintf(output, output_size, "已审核 %d 条 work item -> %s", updated, review_status);
            } else {
                snprintf(output, output_size, "错误：批量审核失败");
            }
        } else {
            err = render_review_queue(output, output_size);
        }
    } else if (strcmp(action, "summary") == 0) {
        err = render_summary(input, output, output_size);
    } else {
        snprintf(output, output_size, "错误：未知 action=%s，支持 add/list/update/summary", action);
        err = ERR_INVALID_ARG;
    }

    cJSON_Delete(input);
    return err;
}

const struct tool *tool_work_item_definition(void)
{
    return &s_work_item_tool;
}
