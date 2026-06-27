#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"

void tool_delegate_sanitize_task_key(const char *src, char *dst, size_t dst_size);
void tool_delegate_append_dependency_csv(char *dst, size_t dst_size, const char *src);
bool tool_delegate_coordinator_dependencies_satisfied(const delegate_coordinator_record_t *record,
                                                      const delegate_coordinator_agent_view_t *agent);
bool tool_delegate_append_dependency_results_context(const char *coordinator_id,
                                                     const char *depends_on_csv,
                                                     char *dst,
                                                     size_t dst_size);
bool tool_delegate_try_render_local_dependency_merge(const delegate_request_t *req,
                                                     const char *coordinator_id,
                                                     char *summary,
                                                     size_t summary_size);
