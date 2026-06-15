#include "compaction.h"

#include "paths.h"
#include "cJSON.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "drivers/memory/session_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "linux/slab.h"
#include "linux/kernel.h"
static err_t recovery_path(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !chat_id[0] || !buf || size == 0) {
        return ERR_INVALID_ARG;
    }
    snprintf(buf, size, "%s/session_%s_recovery.json", daima_path_session_dir(), chat_id);
    return 0;
}

static bool read_text_file(const char *path, char *buf, size_t size)
{
    if (!path || !buf || size == 0) {
        return false;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        buf[0] = '\0';
        return false;
    }
    size_t n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return true;
}

static bool write_text_file(const char *path, const char *text)
{
    if (!path || !text) {
        return false;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    size_t len = strlen(text);
    size_t n = fwrite(text, 1, len, f);
    fclose(f);
    return n == len;
}

static char *trim_ascii(char *s)
{
    if (!s) {
        return s;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        s[--len] = '\0';
    }
    return s;
}

static void append_limited(char *dst, size_t dst_size, const char *text)
{
    if (!dst || dst_size == 0 || !text || !text[0]) {
        return;
    }
    size_t off = strnlen(dst, dst_size);
    if (off >= dst_size - 1) {
        return;
    }
    strscpy(dst + off, text, dst_size - off);
}

static void extract_todos(const char *facts, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!facts || !facts[0]) {
        return;
    }

    char *copy = strdup(facts);
    if (!copy) {
        return;
    }

    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr);
         line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *trimmed = trim_ascii(line);
        if (strncmp(trimmed, "- ", 2) != 0) {
            continue;
        }
        if (out[0]) {
            append_limited(out, out_size, "\n");
        }
        append_limited(out, out_size, trimmed);
    }

    kfree(copy);
}

static void extract_current_task(const char *summary, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!summary || !summary[0]) {
        return;
    }

    char *copy = strdup(summary);
    if (!copy) {
        return;
    }

    char *fallback = NULL;
    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr);
         line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *trimmed = trim_ascii(line);
        if (!trimmed[0] || strncmp(trimmed, "##", 2) == 0 || strncmp(trimmed, "更新时间", strlen("更新时间")) == 0) {
            continue;
        }
        if (!fallback) {
            fallback = trimmed;
        }
        if (strstr(trimmed, "当前任务") || strstr(trimmed, "任务")) {
            strscpy(out, trimmed, out_size);
            break;
        }
    }
    if (!out[0] && fallback) {
        strscpy(out, fallback, out_size);
    }

    kfree(copy);
}

static void extract_last_user_message(const char *chat_id, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    char history[BUF_XLARGE];
    if (session_store_get_history_json(chat_id, history, sizeof(history), SESSION_MAX_MSGS) != 0) {
        return;
    }

    cJSON *messages = cJSON_Parse(history);
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(messages);
        return;
    }

    int count = cJSON_GetArraySize(messages);
    for (int i = count - 1; i >= 0; i--) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (cJSON_IsString(role) && cJSON_IsString(content) && strcmp(role->valuestring, "user") == 0) {
            strscpy(out, content->valuestring ? content->valuestring : "", out_size);
            break;
        }
    }

    cJSON_Delete(messages);
}

static err_t load_recovery(const char *path, compaction_recovery_t *recovery)
{
    if (!path || !recovery) {
        return ERR_INVALID_ARG;
    }
    memset(recovery, 0, sizeof(*recovery));

    char json[BUF_LARGE];
    if (!read_text_file(path, json, sizeof(json))) {
        return ERR_FAIL;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ERR_FAIL;
    }

    cJSON *todos = cJSON_GetObjectItem(root, "active_todos");
    cJSON *last = cJSON_GetObjectItem(root, "last_user_message");
    cJSON *task = cJSON_GetObjectItem(root, "current_task");
    cJSON *snapshot_at = cJSON_GetObjectItem(root, "snapshot_at");
    cJSON *is_valid = cJSON_GetObjectItem(root, "is_valid");

    if (cJSON_IsString(todos)) {
        strscpy(recovery->active_todos, todos->valuestring, sizeof(recovery->active_todos));
    }
    if (cJSON_IsString(last)) {
        strscpy(recovery->last_user_message, last->valuestring, sizeof(recovery->last_user_message));
    }
    if (cJSON_IsString(task)) {
        strscpy(recovery->current_task, task->valuestring, sizeof(recovery->current_task));
    }
    if (cJSON_IsNumber(snapshot_at)) {
        recovery->snapshot_at = (time_t)snapshot_at->valuedouble;
    }
    recovery->is_valid = cJSON_IsBool(is_valid) ? cJSON_IsTrue(is_valid) : false;

    cJSON_Delete(root);
    return 0;
}

err_t compaction_recovery_snapshot(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return ERR_INVALID_ARG;
    }

    compaction_recovery_t recovery;
    memset(&recovery, 0, sizeof(recovery));

    char facts[COMPACTION_RECOVERY_MAX_TODOS];
    char summary[BUF_LARGE];
    facts[0] = '\0';
    summary[0] = '\0';
    session_store_read_facts(chat_id, facts, sizeof(facts));
    session_store_read_summary(chat_id, summary, sizeof(summary));

    extract_todos(facts, recovery.active_todos, sizeof(recovery.active_todos));
    extract_current_task(summary, recovery.current_task, sizeof(recovery.current_task));
    extract_last_user_message(chat_id, recovery.last_user_message, sizeof(recovery.last_user_message));
    recovery.snapshot_at = time(NULL);
    recovery.is_valid = recovery.active_todos[0] || recovery.current_task[0] || recovery.last_user_message[0];

    char path[BUF_SMALL];
    err_t path_err = recovery_path(chat_id, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "active_todos", recovery.active_todos);
    cJSON_AddStringToObject(root, "last_user_message", recovery.last_user_message);
    cJSON_AddStringToObject(root, "current_task", recovery.current_task);
    cJSON_AddNumberToObject(root, "snapshot_at", (double)recovery.snapshot_at);
    cJSON_AddBoolToObject(root, "is_valid", recovery.is_valid);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ERR_NO_MEM;
    }

    bool ok = write_text_file(path, json);
    kfree(json);
    if (!ok) {
        pr_err("Cannot write compaction recovery %s", path);
        return ERR_FAIL;
    }

    pr_info("Compaction recovery snapshot saved for %s", chat_id);
    return 0;
}

err_t compaction_recovery_inject(const char *chat_id, char *system_prompt, size_t system_prompt_size)
{
    if (!chat_id || !chat_id[0] || !system_prompt || system_prompt_size == 0) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_SMALL];
    err_t path_err = recovery_path(chat_id, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    compaction_recovery_t recovery;
    if (load_recovery(path, &recovery) != 0 || !recovery.is_valid) {
        return 0;
    }

    char todos_preview[201];
    const char *todos_src = recovery.active_todos[0] ? recovery.active_todos : "无";
    size_t todos_len = strnlen(todos_src, sizeof(todos_preview) - 1);
    memcpy(todos_preview, todos_src, todos_len);
    todos_preview[todos_len] = '\0';

    size_t off = strnlen(system_prompt, system_prompt_size);
    if (off >= system_prompt_size - 1) {
        return 0;
    }

    int n = snprintf(
        system_prompt + off,
        system_prompt_size - off,
        "\n## 上下文恢复\n"
        "上次压缩时保留的关键信息:\n"
        "- 活跃待办: %s\n"
        "- 当前任务: %s\n"
        "请基于以上信息继续之前的工作。\n",
        todos_preview,
        recovery.current_task[0] ? recovery.current_task : "未记录");
    if (n < 0 || (size_t)n >= system_prompt_size - off) {
        system_prompt[system_prompt_size - 1] = '\0';
    }

    pr_info("Compaction recovery injected for %s", chat_id);
    return 0;
}

err_t compaction_recovery_clear(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return ERR_INVALID_ARG;
    }
    char path[BUF_SMALL];
    err_t path_err = recovery_path(chat_id, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (unlink(path) == 0) {
        pr_info("Compaction recovery cleared for %s", chat_id);
    }
    return 0;
}
