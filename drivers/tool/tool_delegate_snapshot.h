#pragma once

#include <stddef.h>

#include "err.h"

err_t tool_delegate_render_background_coordinator_snapshot(const char *coordinator_id,
                                                           char *output,
                                                           size_t output_size);
err_t tool_delegate_render_parent_registry_view(const char *chat_id,
                                                char *output,
                                                size_t output_size);
