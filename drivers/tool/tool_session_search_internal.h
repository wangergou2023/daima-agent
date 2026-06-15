#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cjson.h"

#define SESSION_SEARCH_CHAT_ID_SIZE  64
#define SESSION_SEARCH_ROLE_SIZE     24
#define SESSION_SEARCH_SNIPPET_SIZE  256
#define SESSION_SEARCH_MAX_SESSIONS  128
#define SESSION_SEARCH_MAX_HITS      256

typedef struct {
    char chat_id[SESSION_SEARCH_CHAT_ID_SIZE];
    int message_hits;
    int fact_hits;
    int summary_hits;
    int total_hits;
    int msg_count;
    bool has_facts;
    bool has_summary;
    time_t latest_ts;
    char first_role[SESSION_SEARCH_ROLE_SIZE];
    char first_snippet[SESSION_SEARCH_SNIPPET_SIZE];
} session_stat_t;

typedef struct {
    char chat_id[SESSION_SEARCH_CHAT_ID_SIZE];
    char source[16];
    char role[SESSION_SEARCH_ROLE_SIZE];
    time_t ts;
    char snippet[SESSION_SEARCH_SNIPPET_SIZE];
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
