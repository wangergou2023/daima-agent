#include "ralph.h"

#include "paths.h"
#include "cJSON.h"
#include "autoconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "linux/slab.h"

static void ralph_loop_build_todo_path(const char *chat_id, char *path, size_t path_size)
{
    char safe_chat_id[128];
    size_t off = 0;
    const char *source = (chat_id && chat_id[0]) ? chat_id : "default";

    for (size_t i = 0; source[i] && off < sizeof(safe_chat_id) - 1; i++) {
        char ch = source[i];
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_') {
            safe_chat_id[off++] = ch;
        } else {
            safe_chat_id[off++] = '_';
        }
    }
    safe_chat_id[off] = '\0';

    snprintf(path, path_size, "%s/session_%s_TODO.json", path_session_dir(), safe_chat_id);
}

static cJSON *ralph_loop_read_json_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 128 * 1024) {
        fclose(f);
        return NULL;
    }

    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    return root;
}

static bool ralph_loop_item_done(cJSON *item)
{
    cJSON *done = cJSON_GetObjectItem(item, "done");
    if (cJSON_IsTrue(done)) {
        return true;
    }
    if (cJSON_IsNumber(done) && done->valueint != 0) {
        return true;
    }

    cJSON *status = cJSON_GetObjectItem(item, "status");
    if (cJSON_IsString(status) && status->valuestring) {
        return strcmp(status->valuestring, "completed") == 0 ||
               strcmp(status->valuestring, "done") == 0 ||
               strcmp(status->valuestring, "cancelled") == 0;
    }
    return false;
}

static bool ralph_loop_has_unfinished_todo(cJSON *root)
{
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!items || !cJSON_IsArray(items)) {
        return false;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (cJSON_IsObject(item) && !ralph_loop_item_done(item)) {
            return true;
        }
    }
    return false;
}

ralph_loop_cfg_t ralph_loop_load_cfg(void)
{
    ralph_loop_cfg_t cfg = {
        .enabled = true,
        .max_iterations = RALPH_LOOP_MAX_ITERATIONS,
        .idle_timeout_ms = RALPH_LOOP_IDLE_TIMEOUT_MS,
    };
    return cfg;
}

bool ralph_loop_should_continue(const char *chat_id, int iteration, const char *final_text)
{
    (void)final_text;

    if (!chat_id || !chat_id[0]) {
        return false;
    }

    ralph_loop_cfg_t cfg = ralph_loop_load_cfg();
    if (!cfg.enabled || iteration >= cfg.max_iterations) {
        return false;
    }

    char path[BUF_PATH];
    ralph_loop_build_todo_path(chat_id, path, sizeof(path));
    cJSON *root = ralph_loop_read_json_file(path);
    bool should_continue = ralph_loop_has_unfinished_todo(root);
    cJSON_Delete(root);
    return should_continue;
}

void ralph_loop_reset(const char *chat_id)
{
    char path[BUF_PATH];
    ralph_loop_build_todo_path(chat_id, path, sizeof(path));
    unlink(path);
}
