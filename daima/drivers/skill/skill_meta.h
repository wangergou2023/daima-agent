#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char title[128];
    char description[256];
} skill_meta_t;

bool skill_meta_validate_name(const char *name);
bool skill_meta_resolve_path(const char *name,
                             const char *file_path,
                             char *resolved,
                             size_t resolved_size);
bool skill_meta_read_file(const char *path, skill_meta_t *meta);
