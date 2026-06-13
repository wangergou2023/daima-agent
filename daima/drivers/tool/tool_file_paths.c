/* 文件工具路径策略：工作区路径与 SPIFFS 路径解析。 */

#include "drivers/tool/tool_file_ops.h"

#include "paths.h"
#include "autoconf.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TOOL_FILES_PATH_SIZE 1024

static bool validate_spiffs_path(const char *path)
{
    if (!path) return false;
    if (strstr(path, "..") != NULL) return false;
    return daima_path_is_in_spiffs(path);
}

static bool get_workspace_root(char *root, size_t root_size)
{
    if (!root || root_size == 0) {
        return false;
    }
    return snprintf(root, root_size, "%s", daima_path_workspace_dir()) < (int)root_size;
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
        if (snprintf(candidate, sizeof(candidate), "%s", path) >= (int)sizeof(candidate)) {
            return false;
        }
    } else {
        if (snprintf(candidate, sizeof(candidate), "%s/%s", workspace_root, path) >= (int)sizeof(candidate)) {
            return false;
        }
    }

    char real_buf[TOOL_FILES_PATH_SIZE];
    if (realpath(candidate, real_buf)) {
        if (!path_is_under_root(real_buf, workspace_root)) {
            return false;
        }
        return snprintf(resolved, resolved_size, "%s", real_buf) < (int)resolved_size;
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
    if (snprintf(leaf, sizeof(leaf), "%s", slash + 1) >= (int)sizeof(leaf)) {
        return false;
    }
    if (!realpath(parent_candidate, real_buf)) {
        return false;
    }
    if (!path_is_under_root(real_buf, workspace_root)) {
        return false;
    }
    return snprintf(resolved, resolved_size, "%s/%s", real_buf, leaf) < (int)resolved_size;
}

bool tool_files_resolve_read_path(const char *path, char *resolved, size_t resolved_size)
{
    if (!path || !path[0] || !resolved || resolved_size == 0) {
        return false;
    }

    if (daima_path_resolve_spiffs_shortcut(path, resolved, resolved_size)) {
        return true;
    }

    if (path[0] != '/') {
        return resolve_workspace_path(path, resolved, resolved_size, false);
    }

    char candidate[TOOL_FILES_PATH_SIZE];
    if (snprintf(candidate, sizeof(candidate), "%s", path) >= (int)sizeof(candidate)) {
        return false;
    }

    char real_buf[TOOL_FILES_PATH_SIZE];
    if (realpath(candidate, real_buf)) {
        if (snprintf(resolved, resolved_size, "%s", real_buf) >= (int)resolved_size) {
            return false;
        }
    } else {
        if (snprintf(resolved, resolved_size, "%s", candidate) >= (int)resolved_size) {
            return false;
        }
    }
    return true;
}

bool tool_files_resolve_write_path(const char *path, char *resolved, size_t resolved_size)
{
    if (daima_path_resolve_spiffs_shortcut(path, resolved, resolved_size)) {
        return true;
    }
    if (validate_spiffs_path(path)) {
        return snprintf(resolved, resolved_size, "%s", path) < (int)resolved_size;
    }
    return resolve_workspace_path(path, resolved, resolved_size, true);
}

bool tool_files_resolve_list_dir_path(const char *path, char *resolved, size_t resolved_size)
{
    if (!path || !path[0]) {
        return resolve_workspace_path(".", resolved, resolved_size, false);
    }
    if (daima_path_resolve_spiffs_shortcut(path, resolved, resolved_size)) {
        return true;
    }
    if (validate_spiffs_path(path)) {
        return snprintf(resolved, resolved_size, "%s", path) < (int)resolved_size;
    }
    return resolve_workspace_path(path, resolved, resolved_size, false);
}

void tool_files_ensure_parent_dirs(const char *path)
{
    if (!path || !path[0]) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (tmp[0] == '\0') return;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}
