#include "app/daima_fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

bool daima_fs_ensure_dir(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

bool daima_fs_ensure_dir_recursive(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (!tmp[0]) {
        return false;
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            daima_fs_ensure_dir(tmp);
            *p = '/';
        }
    }
    return daima_fs_ensure_dir(tmp);
}
