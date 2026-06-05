#pragma once

#include <stddef.h>

#include "memory/session_store.h"

daima_err_t session_store_file_artifact_path(const char *chat_id,
                                             daima_session_artifact_kind_t kind,
                                             char *buf,
                                             size_t size);

daima_err_t session_store_file_read_facts(const char *chat_id, char *buf, size_t size);
daima_err_t session_store_file_merge_facts(const char *chat_id, const char *facts_text);
daima_err_t session_store_file_read_summary(const char *chat_id, char *buf, size_t size);
daima_err_t session_store_file_write_summary(const char *chat_id, const char *summary_text);
