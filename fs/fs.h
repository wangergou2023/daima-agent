/* 文件系统操作接口：目录创建。 */

#pragma once

#include <stdbool.h>

/** 创建单个目录（若已存在也视为成功）。@return 成功返回 true */
bool fs_ensure_dir(const char *path);

/** 递归创建目录树。@return 成功返回 true */
bool fs_ensure_dir_recursive(const char *path);
