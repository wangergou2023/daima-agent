/* 智能体主循环：处理消息、调用大模型、执行工具并回写结果。 */

#include "agent/agent_loop.h"
#include "agent/agent_turn_common.h"
#include "agent/context_compressor.h"
#include "agent/learning_review.h"
#include "agent/agent_turn_finish.h"
#include "agent/agent_turn_prepare.h"
#include "agent/agent_turn_run.h"
#include "bus/message_bus.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"
#include "tools/tool_registry.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static const char *TAG = "agent";

static void agent_loop_task(void *arg)
{
    (void)arg;
    DAIMA_LOGI(TAG, "Agent loop started");

    char *system_prompt = daima_calloc(1, DAIMA_CONTEXT_BUF_SIZE);
    char *history_json = daima_calloc(1, DAIMA_LLM_STREAM_BUF_SIZE);

    if (!system_prompt || !history_json) {
        DAIMA_LOGE(TAG, "Failed to allocate PSRAM buffers");
        free(system_prompt);
        free(history_json);
        return;
    }

    const char *tools_json = tool_registry_get_tools_json();

    while (1) {
        daima_msg_t msg;
        daima_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != DAIMA_OK) continue;

        if (agent_msg_is_internal_control(&msg)) {
            DAIMA_LOGI(TAG, "Dropping internal control message for %s:%s", msg.channel, msg.chat_id);
            agent_cleanup_inbound_msg(&msg);
            continue;
        }

        DAIMA_LOGI(TAG, "Processing message from %s:%s source=%s",
                  msg.channel, msg.chat_id,
                  agent_msg_source_or_default(&msg));

        cJSON *messages = NULL;
        err = agent_turn_prepare(&msg,
                                 system_prompt, DAIMA_CONTEXT_BUF_SIZE,
                                 history_json, DAIMA_LLM_STREAM_BUF_SIZE,
                                 &messages);

        char *final_text = NULL;
        int iteration = 0;
        bool tool_budget_exhausted = false;
        if (err == DAIMA_OK) {
            err = agent_turn_run(system_prompt, messages, tools_json, &msg,
                                 &final_text, &iteration, &tool_budget_exhausted);
        }

        cJSON_Delete(messages);
        agent_turn_finish(&msg, &final_text, err, iteration, tool_budget_exhausted);
    }
}

daima_err_t agent_loop_init(void)
{
    daima_err_t err = context_compressor_init();
    if (err != DAIMA_OK) {
        return err;
    }
    err = learning_review_init();
    if (err != DAIMA_OK) {
        return err;
    }
    DAIMA_LOGI(TAG, "Agent loop initialized");
    return DAIMA_OK;
}

daima_err_t agent_loop_start(void)
{
    const uint32_t stack_candidates[] = {
        DAIMA_AGENT_STACK,
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
            DAIMA_AGENT_PRIO, NULL);

        if (ok) {
            DAIMA_LOGI(TAG, "agent_loop task created with stack=%u bytes", (unsigned)stack_size);
            return DAIMA_OK;
        }

        DAIMA_LOGW(TAG,
                 "agent_loop create failed (stack=%u, free_mem=%u, largest_free=%u), retrying...",
                 (unsigned)stack_size,
                 (unsigned)daima_get_free_memory(),
                 (unsigned)daima_get_largest_free_block());
    }

    return DAIMA_FAIL;
}
