#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "daima_err.h"

typedef struct cJSON cJSON;

typedef struct {
    bool enabled;
    int trigger_msgs;
    int max_chars;
    int protect_first;
    int protect_last;
    int max_passes;
} context_compress_cfg_t;

int context_compress_message_count(const cJSON *messages);
size_t context_compress_estimate_chars(const char *system_prompt, const cJSON *messages);
bool context_compress_needed(const cJSON *messages,
                             const context_compress_cfg_t *cfg,
                             size_t approx_chars);
cJSON *context_compress_load_session_messages(const char *chat_id);
daima_err_t context_compress_compact_once(const char *chat_id,
                                         cJSON **messages_io,
                                         const context_compress_cfg_t *cfg);
void context_compress_session_in_background(const char *chat_id,
                                            const context_compress_cfg_t *cfg);
