/* 会话存储抽象层。 */

#pragma once

#include "core/err.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct cJSON cJSON;

typedef enum {
    DAIMA_SESSION_ARTIFACT_HISTORY = 0,
    DAIMA_SESSION_ARTIFACT_FACTS,
    DAIMA_SESSION_ARTIFACT_SUMMARY,
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
    daima_err_t (*init)(void);
    daima_err_t (*append_ex)(const char *chat_id,
                            const char *role,
                            const char *content,
                            const char *source);
    daima_err_t (*get_history_json)(const char *chat_id, char *buf, size_t size, int max_msgs);
    daima_err_t (*rewrite_from_array)(const char *chat_id, const cJSON *messages);
    daima_err_t (*read_facts)(const char *chat_id, char *buf, size_t size);
    daima_err_t (*merge_facts)(const char *chat_id, const char *facts_text);
    daima_err_t (*read_summary)(const char *chat_id, char *buf, size_t size);
    daima_err_t (*write_summary)(const char *chat_id, const char *summary_text);
    daima_err_t (*clear)(const char *chat_id);
    daima_err_t (*list_records)(daima_session_record_t *records, size_t capacity, int *out_count);
    daima_err_t (*artifact_path)(const char *chat_id,
                                daima_session_artifact_kind_t kind,
                                char *buf,
                                size_t size);
} daima_session_store_ops_t;

daima_err_t session_store_init(void);

daima_err_t session_store_append(const char *chat_id, const char *role, const char *content);
daima_err_t session_store_append_ex(const char *chat_id,
                                   const char *role,
                                   const char *content,
                                   const char *source);
daima_err_t session_store_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs);
daima_err_t session_store_rewrite_from_array(const char *chat_id, const cJSON *messages);
daima_err_t session_store_read_facts(const char *chat_id, char *buf, size_t size);
daima_err_t session_store_merge_facts(const char *chat_id, const char *facts_text);
daima_err_t session_store_read_summary(const char *chat_id, char *buf, size_t size);
daima_err_t session_store_write_summary(const char *chat_id, const char *summary_text);
daima_err_t session_store_clear(const char *chat_id);
daima_err_t session_store_list_records(daima_session_record_t *records, size_t capacity, int *out_count);
daima_err_t session_store_artifact_path(const char *chat_id,
                                       daima_session_artifact_kind_t kind,
                                       char *buf,
                                       size_t size);

const daima_session_store_ops_t *session_store_file_backend(void);
