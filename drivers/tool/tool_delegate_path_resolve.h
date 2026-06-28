#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"

bool tool_delegate_extract_single_absolute_repo_path(const char *prompt,
                                                     char *path,
                                                     size_t path_size);
bool tool_delegate_workspace_repo_root_from_prompt(const char *text,
                                                   char *path,
                                                   size_t path_size);
bool tool_delegate_extract_repo_scoped_path(const char *text,
                                            const char *repo_root,
                                            char *path,
                                            size_t path_size);
bool tool_delegate_resolve_repo_root(const delegate_request_t *req, char *path, size_t path_size);
bool tool_delegate_file_is_directory(const char *path);
const char *tool_delegate_path_basename(const char *path);
