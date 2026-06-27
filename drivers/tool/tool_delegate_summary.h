/* delegate_task coordinator summary helpers */
#pragma once

#include <stddef.h>

#include "delegate/delegate_task_store.h"

void tool_delegate_render_background_coordinator_summary(const delegate_coordinator_record_t *record,
                                                         char *summary,
                                                         size_t summary_size);
