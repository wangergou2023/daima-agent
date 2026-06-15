#include "todo.h"

#include "paths.h"
#include "cJSON.h"
#include "autoconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "linux/slab.h"

typedef struct {
    int todo_count;
    int completed_count;
    int stale_count;
} todo_enforcer_state_t;

static void build_state_path(const char *chat_id, char *path, size_t path_size)
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

    snprintf(path, path_size, "%s/session_%s_enforcer.json", path_session_dir(), safe_chat_id);
}

static bool load_state(const char *chat_id, todo_enforcer_state_t *state)
{
    char path[BUF_PATH];
    build_state_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 16 * 1024) {
        fclose(f);
        return false;
    }

    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *todo_count = cJSON_GetObjectItem(root, "todo_count");
    cJSON *completed_count = cJSON_GetObjectItem(root, "completed_count");
    cJSON *stale_count = cJSON_GetObjectItem(root, "stale_count");
    state->todo_count = cJSON_IsNumber(todo_count) ? todo_count->valueint : 0;
    state->completed_count = cJSON_IsNumber(completed_count) ? completed_count->valueint : 0;
    state->stale_count = cJSON_IsNumber(stale_count) ? stale_count->valueint : 0;
    cJSON_Delete(root);
    return true;
}

static err_t save_state(const char *chat_id, const todo_enforcer_state_t *state)
{
    char path[BUF_PATH];
    build_state_path(chat_id, path, sizeof(path));
    mkdir(path_spiffs_base(), 0700);
    mkdir(path_session_dir(), 0700);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "todo_count", state->todo_count);
    cJSON_AddNumberToObject(root, "completed_count", state->completed_count);
    cJSON_AddNumberToObject(root, "stale_count", state->stale_count);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ERR_NO_MEM;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        kfree(json);
        return ERR_FAIL;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, f);
    fclose(f);
    kfree(json);
    return written == len ? 0 : ERR_FAIL;
}

todo_enforcer_cfg_t todo_enforcer_load_cfg(void)
{
    todo_enforcer_cfg_t cfg = {
        .enabled = true,
        .max_stale_turns = TODO_ENFORCER_MAX_STALE_TURNS,
    };
    return cfg;
}

err_t todo_enforcer_record_progress(const char *chat_id, int todo_count, int completed_count)
{
    if (!chat_id || !chat_id[0]) {
        return ERR_INVALID_ARG;
    }

    if (todo_count < 0) {
        todo_count = 0;
    }
    if (completed_count < 0) {
        completed_count = 0;
    }
    if (completed_count > todo_count) {
        completed_count = todo_count;
    }

    todo_enforcer_state_t previous = {0};
    bool has_previous = load_state(chat_id, &previous);
    todo_enforcer_state_t next = {
        .todo_count = todo_count,
        .completed_count = completed_count,
        .stale_count = 0,
    };

    if (!has_previous && todo_count > completed_count) {
        next.stale_count = 1;
    } else if (has_previous && completed_count <= previous.completed_count && todo_count > completed_count) {
        next.stale_count = previous.stale_count + 1;
    }

    return save_state(chat_id, &next);
}

err_t todo_enforcer_inject_prompt(const char *chat_id, char *system_prompt, size_t system_prompt_size)
{
    if (!chat_id || !chat_id[0] || !system_prompt || system_prompt_size == 0) {
        return ERR_INVALID_ARG;
    }

    todo_enforcer_cfg_t cfg = todo_enforcer_load_cfg();
    if (!cfg.enabled) {
        return 0;
    }

    todo_enforcer_state_t state = {0};
    if (!load_state(chat_id, &state) || state.stale_count < cfg.max_stale_turns) {
        return 0;
    }

    size_t off = strnlen(system_prompt, system_prompt_size - 1);
    if (off >= system_prompt_size - 1) {
        return 0;
    }

    int n = snprintf(
        system_prompt + off,
        system_prompt_size - off,
        "\n## ⚠️ 任务进度警告\n\n"
        "你已经连续 %d 轮没有完成任何待办事项了。\n"
        "当前待办: %d个, 已完成: %d个\n\n"
        "请在本次回复中:\n"
        "1. 明确说明你现在正在处理哪个待办事项\n"
        "2. 或者标记一个阻塞项并说明原因\n"
        "3. 每完成一个步骤后立即调用 todo_update 更新状态\n\n"
        "不要开始新任务，先把当前待办推进。\n",
        state.stale_count,
        state.todo_count,
        state.completed_count);
    if (n < 0 || (size_t)n >= system_prompt_size - off) {
        system_prompt[system_prompt_size - 1] = '\0';
    }
    return 0;
}

err_t todo_enforcer_reset(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_PATH];
    build_state_path(chat_id, path, sizeof(path));
    if (unlink(path) == 0) {
        return 0;
    }
    return 0;
}
