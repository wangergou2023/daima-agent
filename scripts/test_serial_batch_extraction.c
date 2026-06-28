#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cjson.h"
#include "drivers/tool/tool_delegate_repo_batch.h"

static void init_message(struct message *msg, const char *content, enum intent intent)
{
    memset(msg, 0, sizeof(*msg));
    strcpy(msg->channel, CHAN_WEBSOCKET);
    strcpy(msg->chat_id, "web_test");
    strcpy(msg->source, MSG_SOURCE_USER);
    msg->content = (char *)content;
    msg->intent = intent;
}

int main(void)
{
    struct message msg = {0};
    char *json;
    cJSON *root;
    cJSON *tasks;
    cJSON *first_task;
    cJSON *second_task;
    cJSON *depends_on;

    init_message(&msg,
                 "先读 README，再检查 Makefile，最后给我修改建议",
                 INTENT_FIX);
    json = tool_delegate_build_user_prompt_serial_delegate_batch_json(&msg);
    assert(json != NULL);

    root = cJSON_Parse(json);
    assert(root != NULL);
    assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(root, "dispatch_mode")), "staged") == 0);

    tasks = cJSON_GetObjectItem(root, "tasks");
    assert(tasks != NULL && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) == 3);

    first_task = cJSON_GetArrayItem(tasks, 0);
    second_task = cJSON_GetArrayItem(tasks, 1);
    assert(strstr(cJSON_GetStringValue(cJSON_GetObjectItem(first_task, "description")), "读 README") != NULL);
    depends_on = cJSON_GetObjectItem(second_task, "depends_on");
    assert(depends_on != NULL && cJSON_IsArray(depends_on) && cJSON_GetArraySize(depends_on) == 1);
    assert(strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(depends_on, 0)), "task_1") == 0);

    cJSON_Delete(root);
    free(json);

    init_message(&msg,
                 "读 README 并检查 Makefile，然后给我修改建议",
                 INTENT_FIX);
    json = tool_delegate_build_user_prompt_serial_delegate_batch_json(&msg);
    assert(json != NULL);

    root = cJSON_Parse(json);
    assert(root != NULL);
    tasks = cJSON_GetObjectItem(root, "tasks");
    assert(tasks != NULL && cJSON_IsArray(tasks) && cJSON_GetArraySize(tasks) >= 2);
    first_task = cJSON_GetArrayItem(tasks, 0);
    second_task = cJSON_GetArrayItem(tasks, 1);
    assert(strstr(cJSON_GetStringValue(cJSON_GetObjectItem(first_task, "description")), "读 README") != NULL);
    depends_on = cJSON_GetObjectItem(second_task, "depends_on");
    assert(depends_on != NULL && cJSON_IsArray(depends_on) && cJSON_GetArraySize(depends_on) == 1);
    cJSON_Delete(root);
    free(json);

    puts("PASS");
    return 0;
}
