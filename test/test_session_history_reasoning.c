#include "paths.h"
#include "drivers/memory/session_store.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cjson.h"

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static char *build_history_response_json(const char *chat_id)
{
    char history_json[256 * 1024];
    history_json[0] = '\0';
    if (session_store_get_history_json(chat_id, history_json, sizeof(history_json), 64) != 0) {
        return NULL;
    }

    cJSON *messages = cJSON_Parse(history_json);
    assert(messages);

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItemCaseSensitive(msg, "role");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
        if (!role || !cJSON_IsString(role) || !content || !cJSON_IsString(content)) continue;
        if (strcmp(role->valuestring, "assistant") != 0) continue;

        cJSON *parsed = cJSON_Parse(content->valuestring);
        if (!parsed || !cJSON_IsObject(parsed)) {
            cJSON_Delete(parsed);
            continue;
        }
        cJSON *text = cJSON_GetObjectItemCaseSensitive(parsed, "text");
        if (text && cJSON_IsString(text)) {
            cJSON_ReplaceItemInObjectCaseSensitive(msg, "content", cJSON_CreateString(text->valuestring));
            cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(parsed, "reasoning");
            if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
                cJSON_AddStringToObject(msg, "reasoning", reasoning->valuestring);
            }
        }
        cJSON_Delete(parsed);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddItemToObject(root, "messages", messages);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/agent-history-reasoning-%ld", (long)getpid());
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir_p(path_session_dir());

    assert(session_store_init() == 0);
    assert(session_store_append("web_test", "user", "你好") == 0);
    assert(session_store_append("web_test", "assistant", "{\"text\":\"结论\",\"reasoning\":\"先分析再回答\"}") == 0);

    char *json = build_history_response_json("web_test");
    assert(json);

    cJSON *root = cJSON_Parse(json);
    assert(root);
    cJSON *messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
    assert(messages && cJSON_IsArray(messages));
    cJSON *assistant = NULL;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, messages) {
        cJSON *role = cJSON_GetObjectItemCaseSensitive(item, "role");
        if (role && cJSON_IsString(role) && strcmp(role->valuestring, "assistant") == 0) {
            assistant = item;
            break;
        }
    }
    assert(assistant);
    cJSON *content = cJSON_GetObjectItemCaseSensitive(assistant, "content");
    cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(assistant, "reasoning");
    assert(content && cJSON_IsString(content) && strcmp(content->valuestring, "结论") == 0);
    assert(reasoning && cJSON_IsString(reasoning) && strcmp(reasoning->valuestring, "先分析再回答") == 0);

    cJSON_Delete(root);
    free(json);
    printf("session history reasoning tests passed\n");
    return 0;
}
