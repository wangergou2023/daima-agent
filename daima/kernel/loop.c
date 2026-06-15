/* 智能体主循环：处理消息、调用大模型、执行工具并回写结果。 */

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
#include "autoconf.h"
#include "linux/compiler.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "drivers/tool/tool_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "linux/slab.h"
static void agent_loop_task(void *arg)
{
    (void)arg;
    pr_info("Agent loop started");

    char *system_prompt = daima_calloc(1, CONTEXT_BUF_SIZE);
    char *history_json = daima_calloc(1, LLM_STREAM_BUF_SIZE);

    if (unlikely(!system_prompt || !history_json)) {
        pr_err("Failed to allocate PSRAM buffers");
        kfree(system_prompt);
        kfree(history_json);
        return;
    }

    while (1) {
        struct message msg;
        daima_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (unlikely(err != DAIMA_OK)) continue;

        msg.intent = DAIMA_INTENT_OPEN;
        agent_extension_state_reset();

        err = agent_hooks_trigger_intent(&msg);
        if (unlikely(err != DAIMA_OK)) {
            char *final_text = NULL;
            char *reasoning_text = NULL;
            agent_turn_finish(&msg, &final_text, &reasoning_text, err, 0, false, false);
            continue;
        }

        if (agent_msg_is_internal_control(&msg)) {
            pr_info("Dropping internal control message for %s:%s", msg.channel, msg.chat_id);
            agent_cleanup_inbound_msg(&msg);
            continue;
        }

        pr_info("Processing message from %s:%s source=%s", msg.channel, msg.chat_id, agent_msg_source_or_default(&msg));

        uint64_t cancel_token = agent_cancel_begin_turn(msg.chat_id);
        cJSON *messages = NULL;
        system_prompt[0] = '\0';
        err = agent_turn_prepare(&msg,
                                  agent_extension_state_plan(),
                                  system_prompt, CONTEXT_BUF_SIZE,
                                  history_json, LLM_STREAM_BUF_SIZE,
                                  &messages);
        if (likely(err == DAIMA_OK)) {
            err = agent_hooks_trigger_prepare(&msg, system_prompt, CONTEXT_BUF_SIZE, messages);
        }

        char *final_text = NULL;
        char *reasoning_text = NULL;
        int iteration = 0;
        bool tool_budget_exhausted = false;
        bool cancelled = false;
        if (likely(err == DAIMA_OK)) {
            const char *tools_json = tool_registry_get_tools_json_for_channel(msg.channel);
            err = agent_hooks_trigger_replace_run(&msg, system_prompt, messages, tools_json, &final_text);
            if (unlikely(err != DAIMA_OK)) {
                const char *model_override = NULL;
                err = agent_hooks_trigger_before_run(&msg, &model_override, tools_json);
                if (likely(err == DAIMA_OK)) {
                    err = agent_turn_run(system_prompt, messages, tools_json, &msg,
                                  model_override,
                                  cancel_token,
                                  &final_text, &reasoning_text, &iteration, &tool_budget_exhausted, &cancelled);
                }
            } else {
                iteration = 0;
            }
        }

        cJSON_Delete(messages);
        const char *finish_response = final_text ? final_text : "";
        agent_hooks_trigger_finish(&msg, finish_response);
        agent_turn_finish(&msg, &final_text, &reasoning_text, err, iteration, tool_budget_exhausted, cancelled);
    }
}

daima_err_t agent_loop_init(void)
{
    daima_err_t err = context_compressor_init();
    if (unlikely(err != DAIMA_OK)) {
        return err;
    }
    if (runtime_config_get_learning_review_enabled()) {
        err = learning_review_init();
        if (unlikely(err != DAIMA_OK)) {
            return err;
        }
    } else {
        pr_info("Learning review disabled");
    }
    pr_info("Agent loop initialized");
    return DAIMA_OK;
}

daima_err_t agent_loop_start(void)
{
    const uint32_t stack_candidates[] = {
        AGENT_STACK,
        20 * 1024,
        16 * 1024,
        14 * 1024,
        12 * 1024,
    };

    for (size_t i = 0; i < (sizeof(stack_candidates) / sizeof(stack_candidates[0])); i++) {
        uint32_t stack_size = stack_candidates[i];
        bool ok = daima_task_create(
            agent_loop_task, "agent_loop",
            stack_size, NULL,
            AGENT_PRIO, NULL);

        if (ok) {
            pr_info("agent_loop task created with stack=%u bytes", (unsigned)stack_size);
            return DAIMA_OK;
        }

        pr_warn("agent_loop create failed (stack=%u, free_mem=%u, largest_free=%u), retrying...", (unsigned)stack_size, (unsigned)daima_get_free_memory(), (unsigned)daima_get_largest_free_block());
    }

    return DAIMA_FAIL;
}
