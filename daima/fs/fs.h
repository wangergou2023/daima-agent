#pragma once

#include <stdbool.h>

bool fs_ensure_dir(const char *path);
bool fs_ensure_dir_recursive(const char *path);
