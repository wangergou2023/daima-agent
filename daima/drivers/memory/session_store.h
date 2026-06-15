/* 会话存储抽象层。 */

#pragma once

#include "err.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct cJSON cJSON;

typedef enum {
    SESSION_ARTIFACT_HISTORY = 0,
    SESSION_ARTIFACT_FACTS,
    SESSION_ARTIFACT_SUMMARY,
} daima_session_artifact_kind_t;

typedef struct {
    char chat_id[64];
    bool has_history;
    bool has_facts;
    bool has_summary;
    time_t latest_ts;
    char history_path[256];
    char facts_path[256];
    char summary_path[256];
} daima_session_record_t;

typedef struct daima_session_store_ops {
    err_t (*init)(void);
    err_t (*append_ex)(const char *chat_id,
                            const char *role,
                            const char *content,
                            const char *source);
    err_t (*get_history_json)(const char *chat_id, char *buf, size_t size, int max_msgs);
    err_t (*rewrite_from_array)(const char *chat_id, const cJSON *messages);
    err_t (*read_facts)(const char *chat_id, char *buf, size_t size);
    err_t (*merge_facts)(const char *chat_id, const char *facts_text);
    err_t (*read_summary)(const char *chat_id, char *buf, size_t size);
    err_t (*write_summary)(const char *chat_id, const char *summary_text);
    err_t (*clear)(const char *chat_id);
    err_t (*list_records)(daima_session_record_t *records, size_t capacity, int *out_count);
    err_t (*artifact_path)(const char *chat_id,
                                daima_session_artifact_kind_t kind,
                                char *buf,
                                size_t size);
} daima_session_store_ops_t;

err_t session_store_init(void);

err_t session_store_append(const char *chat_id, const char *role, const char *content);
err_t session_store_append_ex(const char *chat_id,
                                   const char *role,
                                   const char *content,
                                   const char *source);
err_t session_store_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs);
err_t session_store_rewrite_from_array(const char *chat_id, const cJSON *messages);
err_t session_store_read_facts(const char *chat_id, char *buf, size_t size);
err_t session_store_merge_facts(const char *chat_id, const char *facts_text);
err_t session_store_read_summary(const char *chat_id, char *buf, size_t size);
err_t session_store_write_summary(const char *chat_id, const char *summary_text);
err_t session_store_clear(const char *chat_id);
err_t session_store_list_records(daima_session_record_t *records, size_t capacity, int *out_count);
err_t session_store_artifact_path(const char *chat_id,
                                       daima_session_artifact_kind_t kind,
                                       char *buf,
                                       size_t size);

const daima_session_store_ops_t *session_store_file_backend(void);
