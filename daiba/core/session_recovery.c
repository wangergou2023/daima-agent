#include "core/session_recovery.h"

#include "core/paths.h"
#include "cJSON.h"
#include "core/config.h"
#include "core/log.h"
#include "drivers/memory/session_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "session_recovery";
static const time_t SESSION_RECOVERY_TTL_SEC = 30 * 60;

static daima_err_t recovery_path(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !chat_id[0] || !buf || size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }
    snprintf(buf, size, "%s/session_%s_recovery.json", daima_path_session_dir(), chat_id);
    return DAIMA_OK;
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

static int session_recovery_turn_count(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return 0;
    }

    char history[DAIMA_BUF_XLARGE];
    if (session_store_get_history_json(chat_id, history, sizeof(history), DAIMA_SESSION_MAX_MSGS) != DAIMA_OK) {
        return 0;
    }

    cJSON *messages = cJSON_Parse(history);
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(messages);
        return 0;
    }

    int turns = 0;
    int count = cJSON_GetArraySize(messages);
    for (int i = 0; i < count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (cJSON_IsString(role) && role->valuestring && strcmp(role->valuestring, "user") == 0) {
            turns++;
        }
    }
    cJSON_Delete(messages);
    return turns;
}

static daima_err_t load_recovery(const char *path, session_recovery_t *recovery)
{
    if (!path || !recovery) {
        return DAIMA_ERR_INVALID_ARG;
    }
    memset(recovery, 0, sizeof(*recovery));

    char json[DAIMA_BUF_XLARGE];
    if (!read_text_file(path, json, sizeof(json))) {
        return DAIMA_FAIL;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return DAIMA_FAIL;
    }

    cJSON *has_crash = cJSON_GetObjectItem(root, "has_crash");
    cJSON *last_user_msg = cJSON_GetObjectItem(root, "last_user_msg");
    cJSON *crash_reason = cJSON_GetObjectItem(root, "crash_reason");
    cJSON *crash_at = cJSON_GetObjectItem(root, "crash_at");
    cJSON *turn_count = cJSON_GetObjectItem(root, "turn_count");

    recovery->has_crash = cJSON_IsBool(has_crash) ? cJSON_IsTrue(has_crash) : false;
    if (cJSON_IsString(last_user_msg) && last_user_msg->valuestring) {
        snprintf(recovery->last_user_msg, sizeof(recovery->last_user_msg), "%s", last_user_msg->valuestring);
    }
    if (cJSON_IsString(crash_reason) && crash_reason->valuestring) {
        snprintf(recovery->crash_reason, sizeof(recovery->crash_reason), "%s", crash_reason->valuestring);
    }
    if (cJSON_IsNumber(crash_at)) {
        recovery->crash_at = (time_t)crash_at->valuedouble;
    }
    if (cJSON_IsNumber(turn_count)) {
        recovery->turn_count = turn_count->valueint;
    }

    cJSON_Delete(root);
    return DAIMA_OK;
}

session_recovery_t session_recovery_check(const char *chat_id)
{
    session_recovery_t recovery;
    memset(&recovery, 0, sizeof(recovery));

    char path[DAIMA_BUF_PATH];
    if (recovery_path(chat_id, path, sizeof(path)) != DAIMA_OK) {
        return recovery;
    }

    if (load_recovery(path, &recovery) != DAIMA_OK || !recovery.has_crash) {
        memset(&recovery, 0, sizeof(recovery));
        return recovery;
    }

    time_t now = time(NULL);
    if (recovery.crash_at <= 0 || now - recovery.crash_at > SESSION_RECOVERY_TTL_SEC) {
        session_recovery_clear(chat_id);
        memset(&recovery, 0, sizeof(recovery));
    }

    return recovery;
}

daima_err_t session_recovery_save_crash(const char *chat_id,
                                         const char *last_user_msg,
                                         const char *crash_reason)
{
    if (!chat_id || !chat_id[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[DAIMA_BUF_PATH];
    daima_err_t path_err = recovery_path(chat_id, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return DAIMA_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(root, "has_crash", true);
    cJSON_AddStringToObject(root, "last_user_msg", last_user_msg ? last_user_msg : "");
    cJSON_AddStringToObject(root, "crash_reason", crash_reason ? crash_reason : "unknown");
    cJSON_AddNumberToObject(root, "crash_at", (double)time(NULL));
    cJSON_AddNumberToObject(root, "turn_count", session_recovery_turn_count(chat_id));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return DAIMA_ERR_NO_MEM;
    }

    bool ok = write_text_file(path, json);
    free(json);
    if (!ok) {
        DAIMA_LOGW(TAG, "Cannot write session recovery %s", path);
        return DAIMA_FAIL;
    }

    DAIMA_LOGI(TAG, "Session recovery snapshot saved for %s", chat_id);
    return DAIMA_OK;
}

daima_err_t session_recovery_inject_prompt(const char *chat_id,
                                            char *system_prompt,
                                            size_t system_prompt_size)
{
    if (!chat_id || !chat_id[0] || !system_prompt || system_prompt_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    session_recovery_t recovery = session_recovery_check(chat_id);
    if (!recovery.has_crash) {
        return DAIMA_OK;
    }

    size_t off = strnlen(system_prompt, system_prompt_size);
    if (off >= system_prompt_size - 1) {
        return DAIMA_OK;
    }

    char time_buf[64];
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&recovery.crash_at, &tm_buf);
    if (tm_info) {
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(time_buf, sizeof(time_buf), "%ld", (long)recovery.crash_at);
    }

    int n = snprintf(
        system_prompt + off,
        system_prompt_size - off,
        "\n## 🔄 会话恢复\n\n"
        "上一轮对话因为以下原因中断: %s\n"
        "中断时间: %s\n\n"
        "你上次正在处理:\n"
        "\"%s\"\n\n"
        "请先回顾之前的进度，然后继续完成未完成的工作。\n"
        "如果已经无法继续，请告知用户并说明原因。\n",
        recovery.crash_reason[0] ? recovery.crash_reason : "unknown",
        time_buf,
        recovery.last_user_msg);
    if (n < 0 || (size_t)n >= system_prompt_size - off) {
        system_prompt[system_prompt_size - 1] = '\0';
    }

    DAIMA_LOGI(TAG, "Session recovery injected for %s", chat_id);
    return DAIMA_OK;
}

void session_recovery_clear(const char *chat_id)
{
    char path[DAIMA_BUF_PATH];
    if (recovery_path(chat_id, path, sizeof(path)) != DAIMA_OK) {
        return;
    }
    if (unlink(path) == 0) {
        DAIMA_LOGI(TAG, "Session recovery cleared for %s", chat_id);
    }
}
