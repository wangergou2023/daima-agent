#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "recovery.h"
#include "paths.h"
#include "cjson.h"

static void make_dir(const char *path)
{
    assert(mkdir(path, 0700) == 0 || access(path, F_OK) == 0);
}

static void recovery_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/session_%s_recovery.json", path_session_dir(), chat_id);
}

static void write_expired_recovery(const char *chat_id)
{
    char path[512];
    recovery_path(chat_id, path, sizeof(path));

    cJSON *root = cJSON_CreateObject();
    assert(root != NULL);
    cJSON_AddBoolToObject(root, "has_crash", true);
    cJSON_AddStringToObject(root, "last_user_msg", "过期任务");
    cJSON_AddStringToObject(root, "crash_reason", "timeout");
    cJSON_AddNumberToObject(root, "crash_at", (double)(time(NULL) - 31 * 60));
    cJSON_AddNumberToObject(root, "turn_count", 3);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    assert(json != NULL);

    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(json, f) >= 0);
    fclose(f);
    free(json);
}

int main(void)
{
    const char *home = "/tmp/session_recovery_test";
    setenv("AGENT_HOME", home, 1);
    make_dir(home);

    char spiffs[1024];
    assert(strlen(home) + strlen("/spiffs_data") + 1 < sizeof(spiffs));
    strcpy(spiffs, home);
    strcat(spiffs, "/spiffs_data");
    make_dir(spiffs);

    char sessions[1024];
    assert(strlen(spiffs) + strlen("/sessions") + 1 < sizeof(sessions));
    strcpy(sessions, spiffs);
    strcat(sessions, "/sessions");
    make_dir(sessions);

    paths_init();

    const char *chat_id = "session_recovery_case";
    assert(session_recovery_save_crash(chat_id, "继续实现会话恢复", "api_error") == 0);

    session_recovery_t rec = session_recovery_check(chat_id);
    assert(rec.has_crash);
    assert(strcmp(rec.last_user_msg, "继续实现会话恢复") == 0);
    assert(strcmp(rec.crash_reason, "api_error") == 0);
    assert(rec.crash_at > 0);
    assert(rec.turn_count >= 0);

    char prompt[4096] = "基础系统提示\n";
    assert(session_recovery_inject_prompt(chat_id, prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "## 🔄 会话恢复") != NULL);
    assert(strstr(prompt, "上一轮对话因为以下原因中断: api_error") != NULL);
    assert(strstr(prompt, "\"继续实现会话恢复\"") != NULL);
    assert(strstr(prompt, "请先回顾之前的进度，然后继续完成未完成的工作。") != NULL);

    session_recovery_clear(chat_id);
    char path[512];
    recovery_path(chat_id, path, sizeof(path));
    assert(access(path, F_OK) != 0);

    const char *expired_chat = "session_recovery_expired";
    write_expired_recovery(expired_chat);
    rec = session_recovery_check(expired_chat);
    assert(!rec.has_crash);
    recovery_path(expired_chat, path, sizeof(path));
    assert(access(path, F_OK) != 0);

    return 0;
}
