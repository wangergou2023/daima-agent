/* 智能体主循环：异步多路复用，支持多用户并发 */
#include "loop.h"
#include "cancel.h"
#include "hooks.h"
#include "turn_common.h"
#include "context_compress.h"
#include "learning.h"
#include "turn_finish.h"
#include "turn_prepare.h"
#include "turn_run.h"
#include "state.h"
#include "runtime.h"
#include "bus.h"
#include "turn_context.h"
#include "turn_dispatch.h"
#include "linux/core_task.h"
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "drivers/tool/tool_registry.h"
#include "cjson.h"
#include "linux/slab.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TURN_BUF_SIZE 131072

static void process_new_message(struct message *msg)
{
    msg->intent = INTENT_OPEN;
    agent_extension_state_reset();

    if (agent_hooks_trigger_intent(msg) != 0) {
        char *ft = NULL, *rt = NULL;
        agent_turn_finish(msg, &ft, &rt, ERR_FAIL, 0, false, false);
        return;
    }

    if (agent_msg_is_internal_control(msg)) {
        pr_info("Dropping internal control %s:%s", msg->channel, msg->chat_id);
        agent_cleanup_inbound_msg(msg);
        return;
    }

    char *sp = platform_calloc(1, TURN_BUF_SIZE);
    char *hj = platform_calloc(1, TURN_BUF_SIZE);
    if (!sp || !hj) { kfree(sp); kfree(hj); return; }

    cJSON *messages = NULL;
    err_t err = agent_turn_prepare(msg, agent_extension_state_plan(),
                                    sp, TURN_BUF_SIZE, hj, TURN_BUF_SIZE, &messages);
    if (err == 0) err = agent_hooks_trigger_prepare(msg, sp, TURN_BUF_SIZE, messages);

    char *ft = NULL, *rt = NULL;
    int it = 0;
    bool tbe = false, cancelled = false;

    if (err == 0) {
        const char *tj = tool_registry_get_tools_json_for_channel(msg->channel);
        err = agent_hooks_trigger_replace_run(msg, sp, messages, tj, &ft);
        if (err != 0) {
            const char *mo = NULL;
            err = agent_hooks_trigger_before_run(msg, &mo, tj);
            if (err == 0) {
                uint64_t ct = agent_cancel_begin_turn(msg->chat_id);
                err = agent_turn_run(sp, messages, tj, msg, mo, ct,
                                      &ft, &rt, &it, &tbe, &cancelled);
            }
        }
    }

    cJSON_Delete(messages);
    kfree(sp); kfree(hj);
    const char *fr = ft ? ft : "";
    agent_hooks_trigger_finish(msg, fr);
    agent_turn_finish(msg, &ft, &rt, err, it, tbe, cancelled);
}

static void process_core_reply(void)
{
    struct core_task reply;
    memset(&reply, 0, sizeof(reply));
    if (core_recv(CORE_SCHEDULER, &reply, 0) != 0) return;

    const char *chat_id = turn_context_find_by_task(reply.id);
    if (!chat_id) {
        pr_warn("loop: orphan task reply %s, discarded", reply.id);
        kfree(reply.payload); kfree(reply.result);
        return;
    }

    struct turn_snapshot *snap = turn_context_load(chat_id);
    if (!snap) {
        kfree(reply.payload); kfree(reply.result);
        return;
    }

    /* 解析工具结果，注入 messages */
    cJSON *root = cJSON_Parse(reply.result ? reply.result : "{}");
    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (results && cJSON_IsArray(results)) {
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
        /* 注入到 messages */
        cJSON *msg_block = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_block, "role", "user");
        cJSON_AddItemToObject(msg_block, "content", tool_result);
        cJSON_AddItemToArray(snap->messages, msg_block);
    }
    cJSON_Delete(root);

    /* 清除 pending，继续调 LLM */
    snap->pending_task_id[0] = '\0';

    char *ft2 = NULL, *rt2 = NULL;
    int it2 = 0;
    bool tbe2 = false, cancelled2 = false;

    struct message resume_msg = {
        .channel = "", .chat_id = "", .source = "", .content = NULL, .reasoning = NULL
    };
    strscpy(resume_msg.channel, snap->channel, sizeof(resume_msg.channel));
    strscpy(resume_msg.chat_id, snap->chat_id, sizeof(resume_msg.chat_id));

    const char *tj = tool_registry_get_tools_json_for_channel(snap->channel);
    err_t err = agent_turn_run(
        snap->system_prompt, snap->messages, tj, &resume_msg,
        NULL, snap->cancel_token,
        &ft2, &rt2, &it2, &tbe2, &cancelled2);

    const char *fr2 = ft2 ? ft2 : "";
    agent_hooks_trigger_finish(&resume_msg, fr2);
    agent_turn_finish(&resume_msg, &ft2, &rt2, err, it2 + snap->iteration, tbe2, cancelled2);

    turn_context_remove(chat_id);
    kfree(reply.payload); kfree(reply.result);
}

static void agent_loop_task(void *arg)
{
    (void)arg;
    pr_info("Agent loop started (async multiplex mode)");

    while (1) {
        /* 1. 优先处理执行核回复 */
        process_core_reply();

        /* 2. 检查新消息 */
        struct message msg;
        memset(&msg, 0, sizeof(msg));
        if (message_bus_pop_inbound(&msg, 0) == 0) {
            process_new_message(&msg);
        } else {
            /* 无新消息也无回复，稍微休眠 */
            usleep(50000);  /* 50ms */
        }
    }
}

err_t agent_loop_init(void)
{
    err_t err = context_compressor_init();
    if (err != 0) return err;
    if (runtime_config_get_learning_review_enabled()) {
        err = learning_review_init();
        if (err != 0) return err;
    } else {
        pr_info("Learning review disabled");
    }
    pr_info("Agent loop initialized");
    return 0;
}

err_t agent_loop_start(void)
{
    const uint32_t stacks[] = { AGENT_STACK, 20480, 16384, 14336, 12288 };
    for (size_t i = 0; i < sizeof(stacks) / sizeof(stacks[0]); i++) {
        if (task_create(agent_loop_task, "agent_loop", stacks[i], NULL, AGENT_PRIO, NULL)) {
            pr_info("agent_loop created stack=%u", (unsigned)stacks[i]);
            return 0;
        }
        pr_warn("agent_loop create failed stack=%u retry", (unsigned)stacks[i]);
    }
    return ERR_FAIL;
}