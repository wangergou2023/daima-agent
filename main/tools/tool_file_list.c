/* 文件列目录辅助层。 */

#include "tools/tool_file_list.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define TOOL_LIST_PATH_SIZE 1024

daima_err_t tool_files_list_dir(const char *resolved_dir,
                               const char *prefix,
                               char *output,
                               size_t output_size,
                               int *count_out)
{
    if (!resolved_dir || !resolved_dir[0] || !output || output_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(resolved_dir);
    if (!dir) {
        return DAIMA_FAIL;
    }

    size_t off = 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && off < output_size - 1) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char full_path[TOOL_LIST_PATH_SIZE];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", resolved_dir, ent->d_name) >= (int)sizeof(full_path)) {
            continue;
        }

        if (prefix && prefix[0] && strncmp(full_path, prefix, strlen(prefix)) != 0) {
            continue;
        }

        int n = snprintf(output + off, output_size - off, "%s\n", full_path);
        if (n < 0) {
            break;
        }
        if ((size_t)n >= output_size - off) {
            off = output_size - 1;
            output[off] = '\0';
            break;
        }
        off += (size_t)n;
        count++;
    }

    closedir(dir);
    if (count_out) {
        *count_out = count;
    }
    return DAIMA_OK;
}
