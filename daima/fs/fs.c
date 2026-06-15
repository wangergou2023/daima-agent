#include "fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "linux/kernel.h"

bool fs_ensure_dir(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

bool fs_ensure_dir_recursive(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }

    char tmp[1024];
    strscpy(tmp, path, sizeof(tmp));
    if (!tmp[0]) {
        return false;
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            fs_ensure_dir(tmp);
            *p = '/';
        }
    }
    return fs_ensure_dir(tmp);
}
