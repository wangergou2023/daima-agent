/* delegate_task scope metadata helpers */
#include "drivers/tool/tool_delegate_scope.h"

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
    if (!path && req->description[0]) {
        if (strstr(req->description, "kernel/turn")) {
            path = "kernel/turn";
        } else if (strstr(req->description, "kernel/tooling")) {
            path = "kernel/tooling";
        } else if (strstr(req->description, "drivers/tool")) {
            path = "drivers/tool";
        } else if (strstr(req->description, "drivers/llm")) {
            path = "drivers/llm";
        } else if (strstr(req->description, "kernel")) {
            path = "kernel";
        }
    }

    if (path && scope_path && scope_path_size > 0) {
        strscpy(scope_path, path, scope_path_size);
    }

    if (!path || strcmp(path, ".") == 0 || strcmp(path, "/") == 0) {
        kind = "repo_root";
        focus = "repo_overview";
    } else if (strstr(path, "kernel/turn")) {
        kind = "subsystem";
        focus = "turn_execution";
    } else if (strstr(path, "kernel/tooling")) {
        kind = "subsystem";
        focus = "coordination";
    } else if (strstr(path, "drivers/tool")) {
        kind = "subsystem";
        focus = "tool_runtime";
    } else if (strstr(path, "drivers/llm")) {
        kind = "subsystem";
        focus = "llm_adapter";
    } else if (strstr(path, "kernel")) {
        kind = "subsystem";
        focus = "execution_kernel";
    } else if (strstr(path, "drivers")) {
        kind = "subsystem";
        focus = "adapter_layer";
    } else if (tool_delegate_file_is_directory(path)) {
        kind = "subsystem";
        focus = "local_overview";
    } else {
        kind = "file";
        focus = "file_analysis";
    }

    if (scope_kind && scope_kind_size > 0) {
        strscpy(scope_kind, kind, scope_kind_size);
    }
    if (analysis_focus && analysis_focus_size > 0) {
        strscpy(analysis_focus, focus, analysis_focus_size);
    }
}
