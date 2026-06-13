/* 文件工具检查点：备份、回滚与最近检查点记录。 */

#include "drivers/tool/tool_file_ops.h"

#include "core/fs.h"
#include "core/paths.h"
#include "core/config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TOOL_FILES_PATH_SIZE 1024
#define TOOL_FILES_RECENT_CHECKPOINTS 16

typedef struct {
    char path[TOOL_FILES_PATH_SIZE];
    char checkpoint[TOOL_FILES_PATH_SIZE];
} recent_checkpoint_t;

static recent_checkpoint_t s_recent_checkpoints[TOOL_FILES_RECENT_CHECKPOINTS];
static int s_recent_checkpoint_cursor = 0;

static void sanitize_path_for_filename(const char *path, char *buf, size_t size)
{
    if (!buf || size == 0) return;
    buf[0] = '\0';
    if (!path || !path[0]) {
        snprintf(buf, size, "unknown");
        return;
    }

    size_t off = 0;
    for (const char *p = path; *p && off + 1 < size; ++p) {
        char ch = *p;
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            buf[off++] = ch;
        } else {
            buf[off++] = '_';
        }
    }
    buf[off] = '\0';
}

static void record_recent_checkpoint(const char *path, const char *checkpoint)
{
    if (!path || !path[0] || !checkpoint || !checkpoint[0]) {
        return;
    }
    recent_checkpoint_t *slot = &s_recent_checkpoints[s_recent_checkpoint_cursor % TOOL_FILES_RECENT_CHECKPOINTS];
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    snprintf(slot->checkpoint, sizeof(slot->checkpoint), "%s", checkpoint);
    s_recent_checkpoint_cursor++;
}

daima_err_t tool_files_checkpoint_before_write(const char *path,
                                              const char *previous_content,
                                              size_t previous_len,
                                              char *checkpoint_path,
                                              size_t checkpoint_path_size)
{
    if (checkpoint_path && checkpoint_path_size > 0) {
        checkpoint_path[0] = '\0';
    }
    if (!path || !path[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!previous_content && previous_len > 0) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!previous_content && access(path, F_OK) != 0) {
        return DAIMA_OK;
    }

    daima_fs_ensure_dir_recursive(daima_path_checkpoint_dir());

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    char slug[128];
    sanitize_path_for_filename(path, slug, sizeof(slug));

    char path_buf[TOOL_FILES_PATH_SIZE];
    snprintf(path_buf, sizeof(path_buf), "%s/%lld_%s.bak",
             daima_path_checkpoint_dir(),
             (long long)ts.tv_sec,
             slug);

    const char *content = previous_content ? previous_content : "";
    size_t content_len = previous_content ? previous_len : 0;
    daima_err_t err = tool_files_write_text_file(path_buf, content, content_len);
    if (err != DAIMA_OK) {
        return err;
    }

    if (checkpoint_path && checkpoint_path_size > 0) {
        snprintf(checkpoint_path, checkpoint_path_size, "%s", path_buf);
    }
    record_recent_checkpoint(path, path_buf);
    return DAIMA_OK;
}

bool tool_files_get_recent_checkpoint(const char *path,
                                      char *checkpoint_path,
                                      size_t checkpoint_path_size)
{
    if (!path || !checkpoint_path || checkpoint_path_size == 0) {
        return false;
    }
    checkpoint_path[0] = '\0';
    for (int i = 0; i < TOOL_FILES_RECENT_CHECKPOINTS; i++) {
        const recent_checkpoint_t *slot =
            &s_recent_checkpoints[(s_recent_checkpoint_cursor - 1 - i + TOOL_FILES_RECENT_CHECKPOINTS * 2) % TOOL_FILES_RECENT_CHECKPOINTS];
        if (slot->path[0] && strcmp(slot->path, path) == 0 && slot->checkpoint[0]) {
            snprintf(checkpoint_path, checkpoint_path_size, "%s", slot->checkpoint);
            return true;
        }
    }
    return false;
}

daima_err_t tool_files_checkpoint_current_file(const char *path,
                                              size_t max_size,
                                              char *checkpoint_path,
                                              size_t checkpoint_path_size)
{
    if (!path || !path[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (checkpoint_path && checkpoint_path_size > 0) {
            checkpoint_path[0] = '\0';
        }
        return DAIMA_OK;
    }

    char *current = NULL;
    size_t current_len = 0;
    daima_err_t err = tool_files_read_optional_text_file(path, max_size, &current, &current_len);
    if (err != DAIMA_OK) {
        return err;
    }

    err = tool_files_checkpoint_before_write(path, current, current_len, checkpoint_path, checkpoint_path_size);
    free(current);
    return err;
}

daima_err_t tool_files_restore_checkpoint(const char *target_path,
                                         const char *checkpoint_path,
                                         size_t max_size,
                                         char *rollback_checkpoint_path,
                                         size_t rollback_checkpoint_path_size)
{
    if (!target_path || !target_path[0] || !checkpoint_path || !checkpoint_path[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char *checkpoint_text = NULL;
    size_t checkpoint_len = 0;
    daima_err_t err = tool_files_read_optional_text_file(checkpoint_path, max_size, &checkpoint_text, &checkpoint_len);
    if (err != DAIMA_OK) {
        return err;
    }

    char *current = NULL;
    size_t current_len = 0;
    err = tool_files_read_optional_text_file(target_path, max_size, &current, &current_len);
    if (err != DAIMA_OK) {
        free(checkpoint_text);
        return err;
    }

    err = tool_files_checkpoint_before_write(
        target_path,
        current,
        current_len,
        rollback_checkpoint_path,
        rollback_checkpoint_path_size);
    free(current);
    if (err != DAIMA_OK) {
        free(checkpoint_text);
        return err;
    }

    tool_files_ensure_parent_dirs(target_path);
    err = tool_files_write_text_file(target_path, checkpoint_text ? checkpoint_text : "", checkpoint_len);
    free(checkpoint_text);
    return err;
}
