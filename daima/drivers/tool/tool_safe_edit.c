#include "drivers/tool/tool_safe_edit.h"

#include "linux/printk.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "linux/slab.h"
#include "linux/kernel.h"

#define SAFE_EDIT_MAX_FILE_SIZE (1024 * 1024)
#define SAFE_EDIT_TTL_SECONDS   (5 * 60)
static safe_edit_fingerprint_t s_fingerprints[SAFE_EDIT_MAX_TRACKED];
static pthread_mutex_t s_fingerprints_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t fnv1a_32(const char *data, size_t len)
{
    uint32_t hash = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

static uint32_t fnv1a_32_update(uint32_t hash, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

static bool is_trackable_path(const char *path)
{
    return path && path[0] &&
           strncmp(path, "http://", 7) != 0 &&
           strncmp(path, "https://", 8) != 0 &&
           strncmp(path, "ftp://", 6) != 0;
}

static int find_fingerprint_locked(const char *path)
{
    for (int i = 0; i < SAFE_EDIT_MAX_TRACKED; i++) {
        if (s_fingerprints[i].valid && strcmp(s_fingerprints[i].file_path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_slot_locked(void)
{
    int oldest = 0;
    for (int i = 0; i < SAFE_EDIT_MAX_TRACKED; i++) {
        if (!s_fingerprints[i].valid) {
            return i;
        }
        if (s_fingerprints[i].read_at < s_fingerprints[oldest].read_at) {
            oldest = i;
        }
    }
    return oldest;
}

static err_t read_line_range_hash(const char *path, int line_start, int line_end, uint32_t *hash_out)
{
    if (!path || !hash_out || line_start <= 0 || line_end < line_start) {
        return ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return ERR_FAIL;
    }
    if (st.st_size >= SAFE_EDIT_MAX_FILE_SIZE) {
        return ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return ERR_FAIL;
    }

    char *line = NULL;
    size_t cap = 0;
    int current_line = 0;
    uint32_t hash = 0x811c9dc5;
    while (getline(&line, &cap, f) != -1) {
        current_line++;
        if (current_line < line_start) {
            continue;
        }
        if (current_line > line_end) {
            break;
        }
        hash = fnv1a_32_update(hash, line, strlen(line));
    }

    kfree(line);
    fclose(f);
    *hash_out = hash;
    return 0;
}

err_t safe_edit_register_read(const char *path, const char *content, int line_start, int line_end)
{
    if (!is_trackable_path(path) || !content) {
        return 0;
    }
    if (strlen(path) >= SAFE_EDIT_PATH_MAX || strlen(content) >= SAFE_EDIT_MAX_FILE_SIZE) {
        return 0;
    }

    pthread_mutex_lock(&s_fingerprints_mutex);
    int slot = find_fingerprint_locked(path);
    if (slot < 0) {
        slot = find_slot_locked();
    }

    strscpy(s_fingerprints[slot].file_path, path, sizeof(s_fingerprints[slot].file_path));
    s_fingerprints[slot].content_hash = fnv1a_32(content, strlen(content));
    s_fingerprints[slot].line_start = line_start;
    s_fingerprints[slot].line_end = line_end;
    s_fingerprints[slot].read_at = time(NULL);
    s_fingerprints[slot].valid = true;
    pthread_mutex_unlock(&s_fingerprints_mutex);

    pr_info("safe_edit register: %s lines=%d..%d", path, line_start, line_end);
    return 0;
}

err_t safe_edit_verify(const char *path, const char *patch_content)
{
    (void)patch_content;
    if (!is_trackable_path(path)) {
        return 0;
    }

    safe_edit_fingerprint_t fp = {0};
    time_t now = time(NULL);
    pthread_mutex_lock(&s_fingerprints_mutex);
    int slot = find_fingerprint_locked(path);
    if (slot < 0) {
        pthread_mutex_unlock(&s_fingerprints_mutex);
        return 0;
    }
    if (now - s_fingerprints[slot].read_at > SAFE_EDIT_TTL_SECONDS) {
        s_fingerprints[slot].valid = false;
        pthread_mutex_unlock(&s_fingerprints_mutex);
        pr_info("safe_edit expired: %s", path);
        return 0;
    }
    fp = s_fingerprints[slot];
    pthread_mutex_unlock(&s_fingerprints_mutex);

    uint32_t current_hash = 0;
    err_t err = read_line_range_hash(path, fp.line_start, fp.line_end, &current_hash);
    if (err != 0) {
        pr_info("safe_edit verify read failed: %s err=%d", path, err);
        return err;
    }
    if (current_hash != fp.content_hash) {
        pr_info("safe_edit mismatch: %s lines=%d..%d", path, fp.line_start, fp.line_end);
        return ERR_INVALID_STATE;
    }

    pr_info("safe_edit verified: %s", path);
    return 0;
}

void safe_edit_clear(const char *path)
{
    if (!path) {
        return;
    }
    pthread_mutex_lock(&s_fingerprints_mutex);
    int slot = find_fingerprint_locked(path);
    if (slot >= 0) {
        s_fingerprints[slot].valid = false;
    }
    pthread_mutex_unlock(&s_fingerprints_mutex);
    pr_info("safe_edit clear: %s", path);
}

void safe_edit_clear_all(void)
{
    pthread_mutex_lock(&s_fingerprints_mutex);
    memset(s_fingerprints, 0, sizeof(s_fingerprints));
    pthread_mutex_unlock(&s_fingerprints_mutex);
    pr_info("safe_edit clear_all");
}
