#pragma once
#include "core/err.h"
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SAFE_EDIT_MAX_TRACKED 16
#define SAFE_EDIT_PATH_MAX    512

typedef struct {
    char file_path[SAFE_EDIT_PATH_MAX];
    uint32_t content_hash;
    int line_start;
    int line_end;
    time_t read_at;
    bool valid;
} safe_edit_fingerprint_t;

daima_err_t safe_edit_register_read(const char *path, const char *content,
                                     int line_start, int line_end);
daima_err_t safe_edit_verify(const char *path, const char *patch_content);
void safe_edit_clear(const char *path);
void safe_edit_clear_all(void);
