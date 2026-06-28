#include "drivers/tool/tool_delegate_path_resolve.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "drivers/tool/tool_files.h"
#include "paths.h"
#include "linux/kernel.h"

static bool extract_single_absolute_path_token(const char *prompt, char *path, size_t path_size)
{
    if (!prompt || !path || path_size == 0) {
        return false;
    }
    path[0] = '\0';

    const char *start = strstr(prompt, "/");
    while (start) {
        unsigned char prev = (start > prompt) ? (unsigned char)start[-1] : '\0';
        const char *end = start;

        if (start > prompt &&
            (prev == '~' ||
             isalnum(prev) ||
             prev == '_' ||
             prev == '-' ||
             prev == '.')) {
            start = strstr(start + 1, "/");
            continue;
        }
        while (*end) {
            unsigned char ch = (unsigned char)*end;
            if (isspace(ch) || ch == '`' || ch == '"' || ch == '\'' ||
                ch == ',' || ch == ')' || ch == '(' || ch == '}' || ch == ']' ||
                ch == ':' || ch == ';') {
                break;
            }
            if (ch & 0x80) {
                break;
            }
            end++;
        }
        size_t len = (size_t)(end - start);
        if (len > 1 && len < path_size) {
            memcpy(path, start, len);
            path[len] = '\0';
            return true;
        }
        start = strstr(end, "/");
    }
    return false;
}

static bool dir_entry_name_case_insensitive_eq(const char *left, const char *right)
{
    if (!left || !right) {
        return false;
    }
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool join_path_checked(const char *base, const char *name, char *out, size_t out_size)
{
    int written;

    if (!base || !name || !out || out_size == 0) {
        return false;
    }
    written = snprintf(out, out_size, "%s%s%s",
                       strcmp(base, "/") == 0 ? "/" : base,
                       strcmp(base, "/") == 0 ? "" : "/",
                       name);
    return written > 0 && (size_t)written < out_size;
}

static bool resolve_existing_path_with_fuzzy_components(const char *input, char *resolved, size_t resolved_size)
{
    char working[512];
    char next[512];
    const char *cursor;

    if (!input || !input[0] || !resolved || resolved_size == 0) {
        return false;
    }
    if (access(input, F_OK) == 0) {
        strscpy(resolved, input, resolved_size);
        return true;
    }

    strscpy(working, "/", sizeof(working));
    cursor = input;
    while (*cursor == '/') {
        cursor++;
    }

    while (*cursor) {
        char segment[128];
        size_t seg_len = 0;
        while (cursor[seg_len] && cursor[seg_len] != '/' && seg_len + 1 < sizeof(segment)) {
            segment[seg_len] = cursor[seg_len];
            seg_len++;
        }
        segment[seg_len] = '\0';
        cursor += seg_len;
        while (*cursor == '/') {
            cursor++;
        }
        if (!segment[0]) {
            continue;
        }

        if (join_path_checked(working, segment, next, sizeof(next)) &&
            access(next, F_OK) == 0) {
            strscpy(working, next, sizeof(working));
            continue;
        }

        DIR *dir = opendir(working);
        if (!dir) {
            return false;
        }

        bool matched = false;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (dir_entry_name_case_insensitive_eq(entry->d_name, segment) ||
                strstr(entry->d_name, segment) != NULL ||
                strstr(segment, entry->d_name) != NULL) {
                if (join_path_checked(working, entry->d_name, next, sizeof(next)) &&
                    access(next, F_OK) == 0) {
                    strscpy(working, next, sizeof(working));
                    matched = true;
                    break;
                }
            }
        }
        closedir(dir);
        if (!matched) {
            return false;
        }
    }

    strscpy(resolved, working, resolved_size);
    return access(resolved, F_OK) == 0;
}

bool tool_delegate_workspace_repo_root_from_prompt(const char *text,
                                                   char *path,
                                                   size_t path_size)
{
    DIR *dir;
    struct dirent *entry;
    char candidate[512];
    const char *workspace = path_workspace_dir();

    if (!text || !text[0] || !workspace || !workspace[0] || !path || path_size == 0) {
        return false;
    }

    dir = opendir(workspace);
    if (!dir) {
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!strstr(text, entry->d_name)) {
            continue;
        }
        if (!join_path_checked(workspace, entry->d_name, candidate, sizeof(candidate))) {
            continue;
        }
        if (!tool_delegate_file_is_directory(candidate)) {
            continue;
        }
        strscpy(path, candidate, path_size);
        closedir(dir);
        return true;
    }

    closedir(dir);
    return false;
}

bool tool_delegate_extract_single_absolute_repo_path(const char *prompt, char *path, size_t path_size)
{
    char raw[512];
    char resolved[512];
    char normalized[512];
    char *slash;

    if (!extract_single_absolute_path_token(prompt, raw, sizeof(raw))) {
        return false;
    }
    while (strlen(raw) > 1) {
        size_t len = strlen(raw);
        char tail = raw[len - 1];
        if (tail != '`' && tail != '"' && tail != '\'' && tail != ')' && tail != ']' && tail != '}') {
            break;
        }
        raw[len - 1] = '\0';
    }
    if (!resolve_existing_path_with_fuzzy_components(raw, resolved, sizeof(resolved))) {
        strscpy(normalized, raw, sizeof(normalized));
        if (!tool_delegate_file_is_directory(normalized)) {
            slash = strrchr(normalized, '/');
            if (slash && slash != normalized) {
                *slash = '\0';
            } else if (slash == normalized) {
                normalized[1] = '\0';
            }
        }
        strscpy(path, normalized, path_size);
        return true;
    }
    strscpy(normalized, resolved, sizeof(normalized));
    if (!tool_delegate_file_is_directory(normalized)) {
        slash = strrchr(normalized, '/');
        if (slash && slash != normalized) {
            *slash = '\0';
        } else if (slash == normalized) {
            normalized[1] = '\0';
        }
    }
    strscpy(path, normalized, path_size);
    return true;
}

bool tool_delegate_extract_repo_scoped_path(const char *text,
                                            const char *repo_root,
                                            char *path,
                                            size_t path_size)
{
    const char *cursor = text;
    char candidate[512];
    const char *repo_name = NULL;

    if (!text || !text[0] || !repo_root || !repo_root[0] || !path || path_size == 0) {
        return false;
    }
    path[0] = '\0';

    repo_name = tool_delegate_path_basename(repo_root);
    if (!repo_name || !repo_name[0]) {
        return false;
    }

    while (cursor && *cursor) {
        const char *match = strstr(cursor, repo_name);
        const char *start = NULL;
        const char *end = NULL;
        size_t len = 0;

        if (!match) {
            break;
        }
        if (match != text) {
            unsigned char prev = (unsigned char)match[-1];
            if (!isspace(prev) && prev != '`' && prev != '"' && prev != '\'' &&
                prev != '(' && prev != '[' && prev != '{' && prev != '/') {
                cursor = match + strlen(repo_name);
                continue;
            }
        }
        start = match + strlen(repo_name);
        if (*start == '/') {
            start++;
        } else {
            cursor = start;
            continue;
        }
        end = start;
        while (*end) {
            unsigned char ch = (unsigned char)*end;
            if (isspace(ch) || ch == '`' || ch == '"' || ch == '\'' ||
                ch == ',' || ch == ')' || ch == '(' || ch == '}' || ch == ']' ||
                ch == ':' || ch == ';') {
                break;
            }
            if (ch & 0x80) {
                break;
            }
            end++;
        }

        len = (size_t)(end - start);
        if (len > 0 && len < sizeof(candidate) &&
            snprintf(candidate, sizeof(candidate), "%s/%.*s", repo_root, (int)len, start) <
                (int)sizeof(candidate) &&
            access(candidate, F_OK) == 0) {
            if (!tool_delegate_file_is_directory(candidate)) {
                char *slash = strrchr(candidate, '/');
                if (slash && slash != candidate) {
                    *slash = '\0';
                }
            }
            strscpy(path, candidate, path_size);
            return true;
        }
        cursor = end;
    }

    return false;
}

bool tool_delegate_resolve_repo_root(const delegate_request_t *req, char *path, size_t path_size)
{
    const char *text = NULL;

    if (!req || !path || path_size == 0) {
        return false;
    }
    path[0] = '\0';
    if (req->target_path[0]) {
        strscpy(path, req->target_path, path_size);
        return true;
    }
    text = req->prompt[0] ? req->prompt : req->description;
    if (tool_delegate_workspace_repo_root_from_prompt(text, path, path_size)) {
        return true;
    }
    if (tool_delegate_extract_single_absolute_repo_path(text, path, path_size)) {
        return true;
    }
    return false;
}

bool tool_delegate_file_is_directory(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        return false;
    }
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

const char *tool_delegate_path_basename(const char *path)
{
    const char *slash;

    if (!path || !path[0]) {
        return "";
    }
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
