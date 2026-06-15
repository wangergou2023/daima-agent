#include "drivers/tool/tool_session_search.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "drivers/tool/tool_session_search_internal.h"
static const struct tool s_session_search_tool = {
    .name = "session_search",
    .description = "搜索历史会话消息和事实卡片，也可列出已有会话。适合回忆之前聊过什么、查看上下文压缩摘要或事实卡片。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"query\":{\"type\":\"string\",\"description\":\"可选搜索词；不传时返回会话概览\"},"
        "\"chat_id\":{\"type\":\"string\",\"description\":\"可选：只搜索某个会话，例如 web_1dfazy\"},"
        "\"target\":{\"type\":\"string\",\"description\":\"messages / facts / summaries / both，默认 both\"},"
        "\"output_mode\":{\"type\":\"string\",\"description\":\"hits / sessions；有 query 默认 hits，无 query 默认 sessions\"},"
        "\"limit\":{\"type\":\"integer\",\"description\":\"返回条数上限（可选）\"},"
        "\"offset\":{\"type\":\"integer\",\"description\":\"分页偏移（可选）\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_session_search_execute,
};

daima_err_t tool_session_search_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *query = cJSON_GetStringValue(cJSON_GetObjectItem(root, "query"));
    const char *chat_id_filter = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
    const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(root, "target"));
    const char *output_mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output_mode"));
    int offset = tool_session_search_clamp_int(tool_session_search_json_get_int_default(root, "offset", 0), 0, 1 << 20);
    int limit = tool_session_search_clamp_int(
        tool_session_search_json_get_int_default(root, "limit", SESSION_SEARCH_DEFAULT_LIMIT),
        1,
        SESSION_SEARCH_MAX_LIMIT);

    if (!target || !target[0]) {
        target = "both";
    }
    if (strcmp(target, "messages") != 0 &&
        strcmp(target, "facts") != 0 &&
        strcmp(target, "summaries") != 0 &&
        strcmp(target, "both") != 0) {
        snprintf(output, output_size, "错误：target 只支持 messages / facts / summaries / both");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    if (!output_mode || !output_mode[0]) {
        output_mode = query && query[0] ? "hits" : "sessions";
    }
    if (strcmp(output_mode, "hits") != 0 && strcmp(output_mode, "sessions") != 0) {
        snprintf(output, output_size, "错误：output_mode 只支持 hits / sessions");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_session_record_t records[SESSION_SEARCH_MAX_SESSIONS];
    int record_count = 0;
    memset(records, 0, sizeof(records));
    if (session_store_list_records(records, SESSION_SEARCH_MAX_SESSIONS, &record_count) != DAIMA_OK) {
        snprintf(output, output_size, "错误：无法枚举会话记录");
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    session_stat_t stats[SESSION_SEARCH_MAX_SESSIONS];
    session_hit_t hits[SESSION_SEARCH_MAX_HITS];
    memset(stats, 0, sizeof(stats));
    memset(hits, 0, sizeof(hits));
    int stats_count = 0;
    int hit_count = 0;
    bool collect_hits = strcmp(output_mode, "hits") == 0;
    bool need_messages = strcmp(target, "facts") != 0 && strcmp(target, "summaries") != 0;
    bool need_facts = strcmp(target, "messages") != 0 && strcmp(target, "summaries") != 0;
    bool need_summaries = strcmp(target, "messages") != 0 && strcmp(target, "facts") != 0;

    for (int i = 0; i < record_count; i++) {
        const daima_session_record_t *record = &records[i];
        if (chat_id_filter && chat_id_filter[0] && strcmp(chat_id_filter, record->chat_id) != 0) {
            continue;
        }

        if (need_messages && record->has_history && record->history_path[0]) {
            tool_session_search_inspect_session_file(
                record->history_path,
                record->chat_id,
                query,
                collect_hits,
                stats,
                &stats_count,
                hits,
                &hit_count);
            tool_session_search_apply_record_latest_ts(stats, &stats_count, record->chat_id, record->latest_ts);
        }

        if (need_facts && record->has_facts && record->facts_path[0]) {
            tool_session_search_inspect_facts_file(
                record->facts_path,
                record->chat_id,
                query,
                collect_hits,
                stats,
                &stats_count,
                hits,
                &hit_count);
            tool_session_search_apply_record_latest_ts(stats, &stats_count, record->chat_id, record->latest_ts);
        }

        if (need_summaries && record->has_summary) {
            tool_session_search_inspect_summary_file(
                record->chat_id,
                query,
                collect_hits,
                stats,
                &stats_count,
                hits,
                &hit_count);
            tool_session_search_apply_record_latest_ts(stats, &stats_count, record->chat_id, record->latest_ts);
        }
    }

    if (query && query[0]) {
        qsort(stats, (size_t)stats_count, sizeof(stats[0]), tool_session_search_compare_stats_desc);
    } else {
        qsort(stats, (size_t)stats_count, sizeof(stats[0]), tool_session_search_compare_stats_recent);
    }
    if (hit_count > 1) {
        qsort(hits, (size_t)hit_count, sizeof(hits[0]), tool_session_search_compare_hits_desc);
    }

    if (strcmp(output_mode, "sessions") == 0) {
        tool_session_search_render_sessions(output, output_size, stats, stats_count, offset, limit, query && query[0]);
    } else {
        tool_session_search_render_hits(output, output_size, query ? query : "", target, hits, hit_count, offset, limit);
    }

    pr_info("session_search: query=%s chat_id=%s target=%s mode=%s stats=%d hits=%d", query && query[0] ? query : "(none)", chat_id_filter && chat_id_filter[0] ? chat_id_filter : "(all)", target, output_mode, stats_count, hit_count);

    cJSON_Delete(root);
    return DAIMA_OK;
}

const struct tool *tool_session_search_definition(void)
{
    return &s_session_search_tool;
}
