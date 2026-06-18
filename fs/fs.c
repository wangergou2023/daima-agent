/* 文件系统操作：目录创建（单级/递归）。 */

#include "fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "linux/kernel.h"

/**
 * 创建单个目录（类似 mkdir -p）。
 * 若目录已存在（errno == EEXIST）也视为成功。
 * @param path 目录路径
 * @return 成功返回 true
 */
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

/**
 * 递归创建目录树。
 * 遍历路径中每个 '/' 分隔符，逐级调用 fs_ensure_dir。
 * @param path 完整目录路径
 * @return 成功返回 true
 */
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

    /* 从第二个字符开始（跳过前导 '/'），逐级创建 */
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            fs_ensure_dir(tmp);
            *p = '/';
        }
    }
    return fs_ensure_dir(tmp);
}
