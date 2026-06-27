/* delegate_task scope metadata helpers */
#pragma once

#include <stddef.h>

#include "drivers/tool/tool_delegate_types.h"

void tool_delegate_infer_scope_metadata(const delegate_request_t *req,
                                        char *scope_path,
                                        size_t scope_path_size,
                                        char *scope_kind,
                                        size_t scope_kind_size,
                                        char *analysis_focus,
                                        size_t analysis_focus_size);
