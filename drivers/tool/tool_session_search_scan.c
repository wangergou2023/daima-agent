#include "drivers/tool/tool_session_search_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cjson.h"
#include "drivers/memory/session_store.h"
#include "linux/kernel.h"

int tool_session_search_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int tool_session_search_json_get_int_default(cJSON *obj, const char *key, int default_value)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

static void trim_line_end(char *line)
{
    if (!line) return;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

static bool match_substr(const char *text, const char *query)
{
    if (!text) return false;
    if (!query || !query[0]) return true;
    return strcasestr(text, query) != NULL;
}

static bool is_utf8_cont_byte(unsigned char ch)
{
    return (ch & 0xC0) == 0x80;
}

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

static void record_first_hit(session_stat_t *stat, const char *role, const char *snippet)
{
    if (!stat || stat->first_snippet[0]) {
        return;
    }
    strscpy(stat->first_role, role ? role : "-", sizeof(stat->first_role));
    strscpy(stat->first_snippet, snippet ? snippet : "", sizeof(stat->first_snippet));
}

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
