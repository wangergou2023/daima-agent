/* 工具执行异步恢复：消费执行核回复，注入 tool_result，并继续 turn。 */
#include "turn_resume.h"

#include "turn_context.h"
#include "turn_pipeline.h"
#include "turn_run.h"
#include "bus.h"
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "drivers/tool/tool_bus_view.h"
#include "cjson.h"

#include <string.h>

static void append_tool_results(cJSON *messages, cJSON *results)
{
    if (!messages || !cJSON_IsArray(messages) || !results || !cJSON_IsArray(results)) {
        return;
    }

    cJSON *tool_result = cJSON_CreateArray();
    cJSON *r;
    cJSON_ArrayForEach(r, results) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "tool_result");
        cJSON_AddStringToObject(block, "tool_use_id",
                                cJSON_GetStringValue(cJSON_GetObjectItem(r, "id")));
        cJSON_AddStringToObject(block, "content",
                                cJSON_GetStringValue(cJSON_GetObjectItem(r, "output")));
        cJSON_AddItemToArray(tool_result, block);
    }

    cJSON *msg_block = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_block, "role", "user");
    cJSON_AddItemToObject(msg_block, "content", tool_result);
    cJSON_AddItemToArray(messages, msg_block);
}

bool agent_turn_resume_poll(void)
{
    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    if (core_recv(CORE_SCHEDULER, &reply, 0) != 0) {
        return false;
    }

    char chat_id[64];
    struct turn_snapshot snap;
    memset(&snap, 0, sizeof(snap));

    if (!turn_context_find_by_task(reply.id, chat_id, sizeof(chat_id))) {
        pr_warn("turn_resume: orphan task reply %s, discarded", reply.id);
        kfree(reply.payload);
        kfree(reply.result);
        return true;
    }

    if (!turn_context_load_copy(chat_id, &snap)) {
        kfree(reply.payload);
        kfree(reply.result);
        return true;
    }

    cJSON *root = cJSON_Parse(reply.result ? reply.result : "{}");
    append_tool_results(snap.messages, cJSON_GetObjectItem(root, "results"));
    cJSON_Delete(root);

    snap.pending_task_id[0] = '\0';

    char *final_text = NULL;
    char *reasoning_text = NULL;
    int iteration = 0;
    bool tool_budget_exhausted = false;
    bool cancelled = false;

    struct message resume_msg = {
        .channel = "", .chat_id = "", .source = "", .content = NULL, .reasoning = NULL
    };
    strscpy(resume_msg.channel, snap.channel, sizeof(resume_msg.channel));
    strscpy(resume_msg.chat_id, snap.chat_id, sizeof(resume_msg.chat_id));

    const char *tools_json = tool_bus_tools_json_for_channel(snap.channel);
    err_t err = agent_turn_run(
        snap.system_prompt, snap.messages, tools_json, &resume_msg,
        NULL, false, 0, snap.cancel_token,
        &final_text, &reasoning_text, &iteration, &tool_budget_exhausted, &cancelled);

    agent_finalize_turn(&resume_msg, &final_text, &reasoning_text, err, iteration,
                        tool_budget_exhausted, cancelled, snap.iteration);

    turn_context_remove(chat_id);
    turn_context_snapshot_cleanup(&snap);
    kfree(reply.payload);
    kfree(reply.result);
    return true;
}
