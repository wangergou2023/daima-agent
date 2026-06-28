#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ipc/bus.h"
#include "drivers/tool/tool_delegate_batch_policy.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_types.h"

typedef struct {
    int count;
    char paths[16][512];
} explicit_multi_scope_group_t;

bool tool_delegate_request_is_bounded_explore_overview(const delegate_request_t *req);
bool tool_delegate_overview_request_preserves_repo_root(const char *prompt, const char *description);
bool tool_delegate_request_prefers_parallel_scope_batch(const delegate_request_t *req);
bool tool_delegate_should_expand_parallel_scope_batch(const delegate_request_t *req);
bool tool_delegate_collect_explicit_multi_scope_paths_from_message(const struct message *msg,
                                                                   explicit_multi_scope_group_t *out);
int tool_delegate_collect_message_absolute_directory_paths(const char *content,
                                                           char items[][512],
                                                           int max_count);
bool tool_delegate_message_has_multiple_explicit_targets(const struct message *msg);
char *tool_delegate_build_user_prompt_scoped_delegate_batch_json(const struct message *msg);
char *tool_delegate_build_user_prompt_generic_delegate_batch_json(const struct message *msg);
char *tool_delegate_build_user_prompt_serial_delegate_batch_json(const struct message *msg);
char *tool_delegate_build_repo_analysis_delegate_batch_json(const struct message *msg);
char *tool_delegate_build_scoped_delegate_batch_json_for_paths(const char *user_prompt,
                                                               const char *primary_path,
                                                               char paths[][512],
                                                               int path_count);
void tool_delegate_fill_parallel_scope_batch_request(const delegate_request_t *req,
                                                     delegate_request_t *batch_req);
char *tool_delegate_build_parallel_scope_batch_json(const delegate_request_t *req,
                                                    bool include_sudo_preflight);
