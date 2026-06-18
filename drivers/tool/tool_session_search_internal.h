/* 会话搜索——内部接口定义。
 * 包含容量限制常量、统计结构体、命中结构体以及扫描/渲染函数声明。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cjson.h"

/* 会话搜索容量限制 */
#define SESSION_SEARCH_CHAT_ID_SIZE  64    /* chat_id 最大长度 */
#define SESSION_SEARCH_ROLE_SIZE     24    /* 角色名最大长度 */
#define SESSION_SEARCH_SNIPPET_SIZE  256   /* 摘要片段最大长度 */
#define SESSION_SEARCH_MAX_SESSIONS  128   /* 最多扫描的会话数 */
#define SESSION_SEARCH_MAX_HITS      256   /* 最多收集的命中数 */

/* 会话级别统计信息 */
typedef struct {
    char chat_id[SESSION_SEARCH_CHAT_ID_SIZE]; /* 会话标识 */
    int message_hits;      /* 消息正文命中数 */
    int fact_hits;         /* 事实命中数 */
    int summary_hits;      /* 摘要命中数 */
    int total_hits;        /* 总命中数 */
    int msg_count;         /* 消息总数 */
    bool has_facts;        /* 存在事实文件 */
    bool has_summary;      /* 存在摘要文件 */
    time_t latest_ts;      /* 最新消息时间戳 */
    char first_role[SESSION_SEARCH_ROLE_SIZE];       /* 首次命中角色 */
    char first_snippet[SESSION_SEARCH_SNIPPET_SIZE]; /* 首次命中摘要 */
} session_stat_t;

/* 单条命中的详细信息 */
typedef struct {
    char chat_id[SESSION_SEARCH_CHAT_ID_SIZE];  /* 所属会话 */
    char source[16];        /* 来源：messages / facts / summaries */
    char role[SESSION_SEARCH_ROLE_SIZE];   /* 消息角色 */
    time_t ts;              /* 时间戳 */
    char snippet[SESSION_SEARCH_SNIPPET_SIZE]; /* 命中摘要 */
} session_hit_t;

int tool_session_search_clamp_int(int value, int min_value, int max_value);
int tool_session_search_json_get_int_default(cJSON *obj, const char *key, int default_value);

void tool_session_search_inspect_session_file(const char *path,
                                              const char *chat_id,
                                              const char *query,
                                              bool collect_hits,
                                              session_stat_t stats[],
                                              int *stats_count,
                                              session_hit_t hits[],
                                              int *hit_count);

void tool_session_search_inspect_facts_file(const char *path,
                                            const char *chat_id,
                                            const char *query,
                                            bool collect_hits,
                                            session_stat_t stats[],
                                            int *stats_count,
                                            session_hit_t hits[],
                                            int *hit_count);

void tool_session_search_inspect_summary_file(const char *chat_id,
                                              const char *query,
                                              bool collect_hits,
                                              session_stat_t stats[],
                                              int *stats_count,
                                              session_hit_t hits[],
                                              int *hit_count);

void tool_session_search_apply_record_latest_ts(session_stat_t stats[],
                                                int *stats_count,
                                                const char *chat_id,
                                                time_t latest_ts);

int tool_session_search_compare_stats_desc(const void *a, const void *b);
int tool_session_search_compare_stats_recent(const void *a, const void *b);
int tool_session_search_compare_hits_desc(const void *a, const void *b);

void tool_session_search_render_sessions(char *output,
                                         size_t output_size,
                                         const session_stat_t stats[],
                                         int stats_count,
                                         int offset,
                                         int limit,
                                         bool has_query);

void tool_session_search_render_hits(char *output,
                                     size_t output_size,
                                     const char *query,
                                     const char *target,
                                     const session_hit_t hits[],
                                     int hit_count,
                                     int offset,
                                     int limit);
