#include "drivers/tool/tool_session_search_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void format_time_brief(time_t ts, char *buf, size_t size)
{
    if (!buf || size == 0) return;
    if (ts <= 0) {
        snprintf(buf, size, "-");
        return;
    }

    struct tm tm_info;
    localtime_r(&ts, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M", &tm_info);
}

int tool_session_search_compare_stats_desc(const void *a, const void *b)
{
    const session_stat_t *sa = (const session_stat_t *)a;
    const session_stat_t *sb = (const session_stat_t *)b;
    if (sa->total_hits != sb->total_hits) {
        return sb->total_hits - sa->total_hits;
    }
    if (sa->latest_ts != sb->latest_ts) {
        return (sa->latest_ts < sb->latest_ts) ? 1 : -1;
    }
    return strcmp(sa->chat_id, sb->chat_id);
}

int tool_session_search_compare_stats_recent(const void *a, const void *b)
{
    const session_stat_t *sa = (const session_stat_t *)a;
    const session_stat_t *sb = (const session_stat_t *)b;
    if (sa->latest_ts != sb->latest_ts) {
        return (sa->latest_ts < sb->latest_ts) ? 1 : -1;
    }
    return strcmp(sa->chat_id, sb->chat_id);
}

int tool_session_search_compare_hits_desc(const void *a, const void *b)
{
    const session_hit_t *ha = (const session_hit_t *)a;
    const session_hit_t *hb = (const session_hit_t *)b;
    if (ha->ts != hb->ts) {
        return (ha->ts < hb->ts) ? 1 : -1;
    }
    return strcmp(ha->chat_id, hb->chat_id);
}

void tool_session_search_render_sessions(char *output,
                                         size_t output_size,
                                         const session_stat_t stats[],
                                         int stats_count,
                                         int offset,
                                         int limit,
                                         bool has_query)
{
    size_t off = snprintf(
        output, output_size,
        "SESSION_SEARCH\nMODE: sessions\nOFFSET: %d\nLIMIT: %d\n\n",
        offset, limit);

    int shown = 0;
    int matched = 0;
    for (int i = 0; i < stats_count; i++) {
        if (has_query && stats[i].total_hits <= 0) {
            continue;
        }
        if (matched++ < offset) {
            continue;
        }
        if (shown >= limit || off >= output_size - 1) {
            break;
        }

        char time_buf[32];
        format_time_brief(stats[i].latest_ts, time_buf, sizeof(time_buf));
        off += snprintf(
            output + off, output_size - off,
            "- %s: hits=%d (msg=%d facts=%d summary=%d), messages=%d, facts=%s, summary=%s, latest=%s\n",
            stats[i].chat_id,
            stats[i].total_hits,
            stats[i].message_hits,
            stats[i].fact_hits,
            stats[i].summary_hits,
            stats[i].msg_count,
            stats[i].has_facts ? "yes" : "no",
            stats[i].has_summary ? "yes" : "no",
            time_buf);
        if (stats[i].first_snippet[0] && off < output_size - 1) {
            off += snprintf(
                output + off, output_size - off,
                "  first_hit[%s]: %s\n",
                stats[i].first_role[0] ? stats[i].first_role : "-",
                stats[i].first_snippet);
        }
        shown++;
    }

    if (shown == 0) {
        snprintf(output + off, output_size - off, "（未找到会话）\n");
    }
}

void tool_session_search_render_hits(char *output,
                                     size_t output_size,
                                     const char *query,
                                     const char *target,
                                     const session_hit_t hits[],
                                     int hit_count,
                                     int offset,
                                     int limit)
{
    size_t off = snprintf(
        output, output_size,
        "SESSION_SEARCH\nMODE: hits\nQUERY: %s\nTARGET: %s\nOFFSET: %d\nLIMIT: %d\n\n",
        query ? query : "",
        target ? target : "both",
        offset,
        limit);

    int shown = 0;
    for (int i = offset; i < hit_count && shown < limit && off < output_size - 1; i++) {
        char time_buf[32];
        format_time_brief(hits[i].ts, time_buf, sizeof(time_buf));
        off += snprintf(
            output + off, output_size - off,
            "- session=%s source=%s role=%s ts=%s\n  %s\n",
            hits[i].chat_id,
            hits[i].source,
            hits[i].role,
            time_buf,
            hits[i].snippet);
        shown++;
    }

    if (shown == 0) {
        snprintf(output + off, output_size - off, "（未找到命中）\n");
    } else if (offset + shown < hit_count) {
        snprintf(output + off, output_size - off,
                 "\n[Hint] 结果已截断，可用 offset=%d 查看下一页。\n",
                 offset + shown);
    }
}
