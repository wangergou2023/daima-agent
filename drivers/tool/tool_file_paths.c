/* 文件工具路径策略：工作区路径与 SPIFFS 路径解析。 */

#include "drivers/tool/tool_file_ops.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/tool/tool_runtime.h"

#include "paths.h"
#include "autoconf.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "linux/kernel.h"
#include "kernel/tooling/delegate/delegate_task_store.h"

#define TOOL_FILES_PATH_SIZE 1024

static bool validate_spiffs_path(const char *path)
{
    if (!path) return false;
    if (strstr(path, "..") != NULL) return false;
    return path_is_in_spiffs(path);
}

static bool get_workspace_root(char *root, size_t root_size)
{
    if (!root || root_size == 0) {
        return false;
    }
    return strscpy(root, path_workspace_dir(), root_size) < root_size;
}

static bool path_is_under_root(const char *path, const char *root)
{
    if (!path || !root) {
        return false;
    }

    size_t root_len = strlen(root);
    if (strncmp(path, root, root_len) != 0) {
        return false;
    }
    return path[root_len] == '\0' || path[root_len] == '/';
}

static bool current_delegate_scope_path(char *scope_path, size_t scope_path_size)
{
    const struct message *msg = tool_runtime_current_message();

    if (!scope_path || scope_path_size == 0) {
        return false;
    }
    scope_path[0] = '\0';
    return tool_invocation_context_delegate_scope_path(msg, scope_path, scope_path_size);
}

static bool enforce_delegate_scope(const char *resolved_path,
                                   bool list_mode,
                                   char *effective_scope,
                                   size_t effective_scope_size)
{
    char scope_path[TOOL_FILES_PATH_SIZE];
    char scope_real[TOOL_FILES_PATH_SIZE];

    if (!resolved_path || !resolved_path[0]) {
        return false;
    }
    if (!current_delegate_scope_path(scope_path, sizeof(scope_path))) {
        return true;
    }

    if (realpath(scope_path, scope_real)) {
        strscpy(scope_path, scope_real, sizeof(scope_path));
    } else {
        char parent_buf[TOOL_FILES_PATH_SIZE];
        const char *slash = strrchr(scope_path, '/');
        if (slash && slash != scope_path) {
            size_t len = (size_t)(slash - scope_path);
            if (len >= sizeof(parent_buf)) {
                len = sizeof(parent_buf) - 1;
            }
            memcpy(parent_buf, scope_path, len);
            parent_buf[len] = '\0';
            if (realpath(parent_buf, scope_real)) {
                const char *leaf = slash + 1;
                if (leaf && leaf[0]) {
                    snprintf(scope_path, sizeof(scope_path), "%s/%s", scope_real, leaf);
                } else {
                    strscpy(scope_path, scope_real, sizeof(scope_path));
                }
            }
        }
    }

    if (effective_scope && effective_scope_size > 0) {
        effective_scope[0] = '\0';
        strscpy(effective_scope, scope_path, effective_scope_size);
    }

    struct stat st;
    if (stat(scope_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
        if (list_mode) {
            const char *slash = strrchr(scope_path, '/');
            if (slash && slash != scope_path) {
                size_t len = (size_t)(slash - scope_path);
                if (len >= sizeof(scope_real)) {
                    len = sizeof(scope_real) - 1;
                }
                memcpy(scope_real, scope_path, len);
                scope_real[len] = '\0';
                if (effective_scope && effective_scope_size > 0) {
                    strscpy(effective_scope, scope_real, effective_scope_size);
                }
                return path_is_under_root(resolved_path, scope_real);
            }
        }
        return strcmp(resolved_path, scope_path) == 0;
    }
    return path_is_under_root(resolved_path, scope_path);
}

static bool resolve_workspace_path(const char *path,
                                   char *resolved,
                                   size_t resolved_size,
                                   bool allow_missing_leaf)
{
    if (!path || !path[0] || !resolved || resolved_size == 0) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }

    char workspace_root[TOOL_FILES_PATH_SIZE];
    if (!get_workspace_root(workspace_root, sizeof(workspace_root))) {
        return false;
    }

    char candidate[TOOL_FILES_PATH_SIZE];
    if (path[0] == '/') {
        if (strscpy(candidate, path, sizeof(candidate)) >= sizeof(candidate)) {
            return false;
        }
    } else {
        int n = snprintf(candidate, sizeof(candidate), "%s/%s", workspace_root, path);
        if (n < 0 || (size_t)n >= sizeof(candidate)) {
            return false;
        }
    }

    char real_buf[TOOL_FILES_PATH_SIZE];
    if (realpath(candidate, real_buf)) {
        if (!path_is_under_root(real_buf, workspace_root)) {
            return false;
        }
        return strscpy(resolved, real_buf, resolved_size) < resolved_size;
    }

    if (!allow_missing_leaf) {
        return false;
    }

    char parent_candidate[TOOL_FILES_PATH_SIZE];
    char leaf[NAME_MAX];
    const char *slash = strrchr(candidate, '/');
    if (!slash || !slash[1]) {
        return false;
    }
    size_t parent_len = (size_t)(slash - candidate);
    if (parent_len == 0 || parent_len >= sizeof(parent_candidate)) {
        return false;
    }
    memcpy(parent_candidate, candidate, parent_len);
    parent_candidate[parent_len] = '\0';
    if (strscpy(leaf, slash + 1, sizeof(leaf)) >= sizeof(leaf)) {
        return false;
    }
    if (!realpath(parent_candidate, real_buf)) {
        return false;
    }
    if (!path_is_under_root(real_buf, workspace_root)) {
        return false;
    }
    {
        int n = snprintf(resolved, resolved_size, "%s/%s", real_buf, leaf);
        return n >= 0 && (size_t)n < resolved_size;
    }
}

bool tool_files_resolve_read_path(const char *path, char *resolved, size_t resolved_size)
{
    char effective_scope[TOOL_FILES_PATH_SIZE];

    if (!path || !path[0] || !resolved || resolved_size == 0) {
        return false;
    }

    if (path_resolve_spiffs_shortcut(path, resolved, resolved_size)) {
        return true;
    }

    if (path[0] != '/') {
        return resolve_workspace_path(path, resolved, resolved_size, false);
    }

    char candidate[TOOL_FILES_PATH_SIZE];
    if (strscpy(candidate, path, sizeof(candidate)) >= sizeof(candidate)) {
        return false;
    }

    char real_buf[TOOL_FILES_PATH_SIZE];
    if (realpath(candidate, real_buf)) {
        if (strscpy(resolved, real_buf, resolved_size) >= resolved_size) {
            return false;
        }
    } else {
        if (strscpy(resolved, candidate, resolved_size) >= resolved_size) {
            return false;
        }
    }
    if (!enforce_delegate_scope(resolved, false, effective_scope, sizeof(effective_scope))) {
        pr_warn("delegate scoped read rejected: path=%s scope=%s",
                resolved,
                effective_scope[0] ? effective_scope : "-");
        return false;
    }
    return true;
}

bool tool_files_resolve_write_path(const char *path, char *resolved, size_t resolved_size)
{
    if (path_resolve_spiffs_shortcut(path, resolved, resolved_size)) {
        return true;
    }
    if (validate_spiffs_path(path)) {
        return strscpy(resolved, path, resolved_size) < resolved_size;
    }
    return resolve_workspace_path(path, resolved, resolved_size, true);
}

bool tool_files_resolve_list_dir_path(const char *path, char *resolved, size_t resolved_size)
{
    char effective_scope[TOOL_FILES_PATH_SIZE];

    if (!path || !path[0]) {
        if (current_delegate_scope_path(effective_scope, sizeof(effective_scope))) {
            return tool_files_resolve_read_path(effective_scope, resolved, resolved_size);
        }
        return resolve_workspace_path(".", resolved, resolved_size, false);
    }
    if (!tool_files_resolve_read_path(path, resolved, resolved_size)) {
        return false;
    }
    if (!enforce_delegate_scope(resolved, true, effective_scope, sizeof(effective_scope))) {
        pr_warn("delegate scoped list/search rejected: path=%s scope=%s",
                resolved,
                effective_scope[0] ? effective_scope : "-");
        return false;
    }
    return true;
}

void tool_files_ensure_parent_dirs(const char *path)
{
    if (!path || !path[0]) return;
    char tmp[512];
    strscpy(tmp, path, sizeof(tmp));
    if (tmp[0] == '\0') return;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}
