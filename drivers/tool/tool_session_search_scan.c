/* 会话搜索——文件系统扫描实现。
 * 遍历会话消息文件和事实文件，按查询字符串匹配并收集结果。
 * 支持分片（snippet）生成、UTF-8 安全截断、命中统计与分页。 */

#include "drivers/tool/tool_session_search_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cjson.h"
#include "drivers/memory/session_store.h"
#include "linux/kernel.h"

/**
 * 将整数值钳制到 [min_value, max_value] 区间。
 * @param value     输入值
 * @param min_value 下限
 * @param max_value 上限
 * @return 钳制后的值
 */
int tool_session_search_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

/**
 * 从 cJSON 对象中读取整数字段，若不存在则返回默认值。
 * @param obj           目标 JSON 对象
 * @param key           字段名
 * @param default_value 字段不存在时的默认值
 * @return 字段值或默认值
 */
int tool_session_search_json_get_int_default(cJSON *obj, const char *key, int default_value)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

/* 去除行尾的 \n \r 字符，原地修改。 */
static void trim_line_end(char *line)
{
    if (!line) return;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

/* 大小写不敏感子串匹配。query 为空时匹配一切。 */
static bool match_substr(const char *text, const char *query)
{
    if (!text) return false;
    if (!query || !query[0]) return true;
    return strcasestr(text, query) != NULL;
}

/* 判断字节是否为 UTF-8 多字节序列的后续字节（10xxxxxx）。 */
static bool is_utf8_cont_byte(unsigned char ch)
{
    return (ch & 0xC0) == 0x80;
}

/**
 * 从文本中构建搜索摘要片段。
 * 若匹配长度小于输出缓冲区，直接复制全文；否则以匹配位置为中心截取，
 * 保证不会在 UTF-8 多字节序列中间截断。
 * @param text     原始文本
 * @param query    搜索关键词（用于定位匹配位置）
 * @param out      输出缓冲区
 * @param out_size 缓冲区大小
 */
static void build_snippet(const char *text, const char *query, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!text || !text[0]) return;

    const char *match = (query && query[0]) ? strcasestr(text, query) : text;
    size_t len = strlen(text);
    if (!match || len <= out_size - 1) {
        strscpy(out, text, out_size);
        return;
    }

    size_t match_pos = (size_t)(match - text);
    size_t start = match_pos > 72 ? match_pos - 72 : 0;
    size_t end = start + (out_size > 7 ? out_size - 7 : out_size - 1);
    if (end > len) {
        end = len;
    }
    while (start > 0 && is_utf8_cont_byte((unsigned char)text[start])) {
        start--;
    }
    while (end > start && end < len && is_utf8_cont_byte((unsigned char)text[end])) {
        end--;
    }

    size_t off = 0;
    if (start > 0 && out_size > 4) {
        off += snprintf(out + off, out_size - off, "...");
    }

    size_t copy = end > start ? (end - start) : 0;
    if (copy > 0 && off < out_size - 1) {
        if (copy > out_size - off - 1) {
            copy = out_size - off - 1;
        }
        memcpy(out + off, text + start, copy);
        off += copy;
        out[off] = '\0';
    }

    if (end < len && off < out_size - 1) {
        snprintf(out + off, out_size - off, "...");
    }
}

/* 在统计数组中查找或新增一个会话条目，返回索引（-1 表示已满）。 */
static int find_or_add_session(session_stat_t stats[], int *count, const char *chat_id)
{
    for (int i = 0; i < *count; i++) {
        if (strcmp(stats[i].chat_id, chat_id) == 0) {
            return i;
        }
    }

    if (*count >= SESSION_SEARCH_MAX_SESSIONS) {
        return -1;
    }

    int idx = *count;
    memset(&stats[idx], 0, sizeof(stats[idx]));
    strscpy(stats[idx].chat_id, chat_id, sizeof(stats[idx].chat_id));
    (*count)++;
    return idx;
}

/* 仅当首次命中时记录角色和摘要片段。 */
static void record_first_hit(session_stat_t *stat, const char *role, const char *snippet)
{
    if (!stat || stat->first_snippet[0]) {
        return;
    }
    strscpy(stat->first_role, role ? role : "-", sizeof(stat->first_role));
    strscpy(stat->first_snippet, snippet ? snippet : "", sizeof(stat->first_snippet));
}

/* 向命中列表追加一条记录，超过上限时静默丢弃。 */
static void append_hit(session_hit_t hits[],
                       int *hit_count,
                       const char *chat_id,
                       const char *source,
                       const char *role,
                       time_t ts,
                       const char *snippet)
{
    if (*hit_count >= SESSION_SEARCH_MAX_HITS) {
        return;
    }

    session_hit_t *hit = &hits[*hit_count];
    memset(hit, 0, sizeof(*hit));
    strscpy(hit->chat_id, chat_id, sizeof(hit->chat_id));
    strscpy(hit->source, source ? source : "-", sizeof(hit->source));
    strscpy(hit->role, role ? role : "-", sizeof(hit->role));
    hit->ts = ts;
    strscpy(hit->snippet, snippet ? snippet : "", sizeof(hit->snippet));
    (*hit_count)++;
}

/**
 * 更新指定 chat_id 的最新时间戳（若当前值更近）。
 * @param stats       会话统计数组
 * @param stats_count 会话计数指针
 * @param chat_id     会话标识
 * @param latest_ts   候选时间戳
 */
void tool_session_search_apply_record_latest_ts(session_stat_t stats[],
                                                int *stats_count,
                                                const char *chat_id,
                                                time_t latest_ts)
{
    if (!chat_id || latest_ts <= 0) {
        return;
    }
    int idx = find_or_add_session(stats, stats_count, chat_id);
    if (idx >= 0 && latest_ts > stats[idx].latest_ts) {
        stats[idx].latest_ts = latest_ts;
    }
}

/**
 * 扫描单个会话消息文件（JSONL 格式）。
 * 逐行解析 JSON，若 query 非空则进行大小写不敏感匹配，
 * 命中时生成摘要片段并记录到统计和命中列表。
 * @param path        会话文件路径
 * @param chat_id     会话标识
 * @param query       搜索关键词（NULL 或空串表示不过滤）
 * @param collect_hits 是否收集详细命中记录
 * @param stats       会话统计数组
 * @param stats_count 会话计数指针
 * @param hits        命中记录数组
 * @param hit_count   命中计数指针
 */
void tool_session_search_inspect_session_file(const char *path,
                                              const char *chat_id,
                                              const char *query,
                                              bool collect_hits,
                                              session_stat_t stats[],
                                              int *stats_count,
                                              session_hit_t hits[],
                                              int *hit_count)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    int stat_idx = find_or_add_session(stats, stats_count, chat_id);
    char line[16384];
    while (fgets(line, sizeof(line), f)) {
        trim_line_end(line);
        if (!line[0]) {
            continue;
        }

        cJSON *obj = cJSON_Parse(line);
        if (!obj) {
            continue;
        }

        cJSON *role = cJSON_GetObjectItem(obj, "role");
        cJSON *content = cJSON_GetObjectItem(obj, "content");
        cJSON *ts = cJSON_GetObjectItem(obj, "ts");
        const char *role_str = (role && cJSON_IsString(role) && role->valuestring) ? role->valuestring : "-";
        const char *content_str = (content && cJSON_IsString(content) && content->valuestring) ? content->valuestring : "";
        time_t ts_value = (ts && cJSON_IsNumber(ts)) ? (time_t)ts->valuedouble : 0;

        if (stat_idx >= 0) {
            stats[stat_idx].msg_count++;
            if (ts_value > stats[stat_idx].latest_ts) {
                stats[stat_idx].latest_ts = ts_value;
            }
        }

        if (query && query[0] && match_substr(content_str, query)) {
            char snippet[SESSION_SEARCH_SNIPPET_SIZE];
            build_snippet(content_str, query, snippet, sizeof(snippet));

            if (stat_idx >= 0) {
                stats[stat_idx].message_hits++;
                stats[stat_idx].total_hits++;
                record_first_hit(&stats[stat_idx], role_str, snippet);
            }
            if (collect_hits) {
                append_hit(hits, hit_count, chat_id, "messages", role_str, ts_value, snippet);
            }
        }

        cJSON_Delete(obj);
    }

    fclose(f);
}

/**
 * 扫描单个会话事实文件（Markdown 格式）。
 * 跳过 "##" 标题行和列表符号前缀，对正文内容进行匹配。
 * @param path        事实文件路径
 * @param chat_id     会话标识
 * @param query       搜索关键词
 * @param collect_hits 是否收集详细命中记录
 * @param stats       会话统计数组
 * @param stats_count 会话计数指针
 * @param hits        命中记录数组
 * @param hit_count   命中计数指针
 */
void tool_session_search_inspect_facts_file(const char *path,
                                            const char *chat_id,
                                            const char *query,
                                            bool collect_hits,
                                            session_stat_t stats[],
                                            int *stats_count,
                                            session_hit_t hits[],
                                            int *hit_count)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    int stat_idx = find_or_add_session(stats, stats_count, chat_id);
    if (stat_idx >= 0) {
        stats[stat_idx].has_facts = true;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim_line_end(line);
        if (!line[0] || strncmp(line, "##", 2) == 0) {
            continue;
        }

        const char *content = line;
        if ((content[0] == '-' || content[0] == '*' || content[0] == '+') && content[1] == ' ') {
            content += 2;
        }

        if (query && query[0] && match_substr(content, query)) {
            char snippet[SESSION_SEARCH_SNIPPET_SIZE];
            build_snippet(content, query, snippet, sizeof(snippet));

            if (stat_idx >= 0) {
                stats[stat_idx].fact_hits++;
                stats[stat_idx].total_hits++;
                record_first_hit(&stats[stat_idx], "facts", snippet);
            }
            if (collect_hits) {
                append_hit(hits, hit_count, chat_id, "facts", "facts", 0, snippet);
            }
        }
    }

    fclose(f);
}

/**
 * 读取并扫描会话摘要文件。
 * 通过 session_store_read_summary 读取摘要内容后进行匹配。
 * @param chat_id     会话标识
 * @param query       搜索关键词
 * @param collect_hits 是否收集详细命中记录
 * @param stats       会话统计数组
 * @param stats_count 会话计数指针
 * @param hits        命中记录数组
 * @param hit_count   命中计数指针
 */
void tool_session_search_inspect_summary_file(const char *chat_id,
                                              const char *query,
                                              bool collect_hits,
                                              session_stat_t stats[],
                                              int *stats_count,
                                              session_hit_t hits[],
                                              int *hit_count)
{
    char summary_buf[2048];
    if (session_store_read_summary(chat_id, summary_buf, sizeof(summary_buf)) != 0 || !summary_buf[0]) {
        return;
    }

    int stat_idx = find_or_add_session(stats, stats_count, chat_id);
    if (stat_idx >= 0) {
        stats[stat_idx].has_summary = true;
    }

    if (query && query[0] && match_substr(summary_buf, query)) {
        char snippet[SESSION_SEARCH_SNIPPET_SIZE];
        build_snippet(summary_buf, query, snippet, sizeof(snippet));

        if (stat_idx >= 0) {
            stats[stat_idx].summary_hits++;
            stats[stat_idx].total_hits++;
            record_first_hit(&stats[stat_idx], "summary", snippet);
        }
        if (collect_hits) {
            append_hit(hits, hit_count, chat_id, "summaries", "summary", 0, snippet);
        }
    }
}
