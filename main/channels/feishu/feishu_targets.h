#pragma once

#include <stdbool.h>
#include <stddef.h>

bool feishu_targets_record(const char *route_id,
                           const char *chat_id,
                           const char *chat_type,
                           const char *sender_id);

bool feishu_targets_get_default(char *out, size_t out_size);
