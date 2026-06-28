/* delegate_task scope metadata helpers */
#include "drivers/tool/tool_delegate_scope.h"

#include "drivers/tool/tool_delegate_overview.h"
#include "drivers/tool/tool_delegate_repo_batch.h"

#include "linux/kernel.h"
#include "text.h"

#include <string.h>

void tool_delegate_infer_scope_metadata(const delegate_request_t *req,
                                        char *scope_path,
                                        size_t scope_path_size,
                                        char *scope_kind,
                                        size_t scope_kind_size,
                                        char *analysis_focus,
                                        size_t analysis_focus_size)
{
    const char *path = NULL;
    const char *kind = "task";
    const char *focus = "general";
    bool is_directory = false;

    if (scope_path && scope_path_size > 0) {
        scope_path[0] = '\0';
    }
    if (scope_kind && scope_kind_size > 0) {
        scope_kind[0] = '\0';
    }
    if (analysis_focus && analysis_focus_size > 0) {
        analysis_focus[0] = '\0';
    }
    if (!req) {
        return;
    }

    path = req->target_path[0] ? req->target_path : NULL;
    if (path && scope_path && scope_path_size > 0) {
        strscpy(scope_path, path, scope_path_size);
    }

    is_directory = path && tool_delegate_file_is_directory(path);
    if (!path || strcmp(path, ".") == 0 || strcmp(path, "/") == 0) {
        kind = "repo_root";
        focus = "root_scope";
    } else if (is_directory) {
        kind = "subsystem";
        if (strcmp(req->subagent_type, "explore") == 0) {
            focus = tool_delegate_request_requires_deeper_explore_analysis(req)
                ? "bounded_deep_analysis"
                : "scoped_structure_analysis";
        } else if (strcmp(req->subagent_type, "implement") == 0) {
            focus = "implementation_scope";
        } else if (strcmp(req->subagent_type, "oracle") == 0) {
            focus = "architecture_scope";
        } else if (strcmp(req->subagent_type, "librarian") == 0) {
            focus = "reference_scope";
        } else {
            focus = "subsystem_scope";
        }
    } else {
        kind = "file";
        if (strcmp(req->subagent_type, "implement") == 0) {
            focus = "implementation_file";
        } else {
            focus = "file_analysis";
        }
    }

    if (scope_kind && scope_kind_size > 0) {
        strscpy(scope_kind, kind, scope_kind_size);
    }
    if (analysis_focus && analysis_focus_size > 0) {
        strscpy(analysis_focus, focus, analysis_focus_size);
    }
}
