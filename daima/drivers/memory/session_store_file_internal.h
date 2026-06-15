#pragma once

#include <stddef.h>

#include "autoconf.h"
#include "drivers/memory/session_store.h"

err_t session_store_file_artifact_path(const char *chat_id,
                                             daima_session_artifact_kind_t kind,
                                             char *buf,
                                             size_t size);

err_t session_store_file_read_facts(const char *chat_id, char *buf, size_t size);
err_t session_store_file_merge_facts(const char *chat_id, const char *facts_text);
err_t session_store_file_read_summary(const char *chat_id, char *buf, size_t size);
err_t session_store_file_write_summary(const char *chat_id, const char *summary_text);
