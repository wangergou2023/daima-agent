/* 智能体主循环：处理消息、调用大模型、执行工具并回写结果。 */

#include "agent/agent_loop.h"
#include "agent/agent_cancel.h"
#include "agent/agent_coordinator.h"
#include "agent/agent_roles.h"
#include "agent/agent_turn_common.h"
#include "agent/context_compressor.h"
#include "agent/learning_review.h"
#include "agent/plan_review.h"
#include "agent/agent_turn_finish.h"
#include "agent/agent_turn_prepare.h"
#include "agent/agent_turn_run.h"
#include "agent/intent_gate.h"
#include "agent/team_mode.h"
#include "app/runtime_config.h"
#include "bus/message_bus.h"
#include "daima_config.h"
#include "daima_log.h"
#include "daima_os.h"
#include "daima_platform.h"
#include "tools/tool_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static const char *TAG = "agent";

#ifdef DAIMA_AGENT_ROLES_ENABLED
static agent_role_t agent_loop_active_role(const agent_role_t roles[3], int role_count,
                                           const daima_plan_t *plan)
{
    if (role_count <= 0) {
        return AGENT_ROLE_FAST;
    }
#ifdef DAIMA_PLAN_REVIEW_ENABLED
    if (plan && plan->has_plan && plan->reviewed && role_count > 1) {
        return roles[1];
    }
#else
    (void)plan;
#endif
    return roles[0];
}

static void agent_loop_append_role_prompt(char *system_prompt, size_t system_prompt_size,
                                          agent_role_t role)
{
    if (!system_prompt || system_prompt_size == 0) {
        return;
    }

    size_t prompt_len = strnlen(system_prompt, system_prompt_size - 1);
    if (prompt_len >= system_prompt_size - 1) {
        return;
    }

    int written = snprintf(system_prompt + prompt_len,
                           system_prompt_size - prompt_len,
                           "\n\n## 当前角色: %s\n%s\n",
                           agent_role_name(role),
                           agent_role_prompt_suffix(role));
    if (written < 0 || (size_t)written >= system_prompt_size - prompt_len) {
        system_prompt[system_prompt_size - 1] = '\0';
    }
}
#endif

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

    while (1) {
        daima_msg_t msg;
        daima_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != DAIMA_OK) continue;

        msg.intent = DAIMA_INTENT_OPEN;

#ifdef DAIMA_PLAN_REVIEW_ENABLED
        daima_plan_t plan = {0};
#endif
#ifdef DAIMA_AGENT_ROLES_ENABLED
        agent_role_t roles[3] = {0};
        int role_count = 0;
        agent_role_t active_role = AGENT_ROLE_FAST;
#endif

#ifdef DAIMA_INTENT_GATE_ENABLED
        intent_gate_classify(msg.content, &msg.intent);
        DAIMA_LOGI(TAG, "Intent classified: %s -> %s", msg.content, daima_intent_name(msg.intent));
#ifdef DAIMA_PLAN_REVIEW_ENABLED
        if (msg.intent == DAIMA_INTENT_IMPLEMENT || msg.intent == DAIMA_INTENT_FIX) {
            daima_err_t plan_err = plan_review_generate(msg.intent, msg.content, system_prompt, &plan);
            if (plan_err == DAIMA_OK && plan.has_plan && plan.reviewed) {
                DAIMA_LOGI(TAG, "Plan generated and reviewed for intent=%s", daima_intent_name(msg.intent));
            }
        }
#endif
#endif
#ifdef DAIMA_AGENT_ROLES_ENABLED
        role_count = agent_roles_for_intent(msg.intent, roles);
        active_role = agent_loop_active_role(roles, role_count,
#ifdef DAIMA_PLAN_REVIEW_ENABLED
                                             &plan
#else
                                             NULL
#endif
        );
        if (role_count > 0) {
            DAIMA_LOGI(TAG, "Agent roles for intent=%s: %s (chain of %d)",
                       daima_intent_name(msg.intent), agent_role_name(roles[0]), role_count);
        }
#endif

        if (agent_msg_is_internal_control(&msg)) {
            DAIMA_LOGI(TAG, "Dropping internal control message for %s:%s", msg.channel, msg.chat_id);
            agent_cleanup_inbound_msg(&msg);
            continue;
        }

        DAIMA_LOGI(TAG, "Processing message from %s:%s source=%s",
                  msg.channel, msg.chat_id,
                  agent_msg_source_or_default(&msg));

        uint64_t cancel_token = agent_cancel_begin_turn(msg.chat_id);
        cJSON *messages = NULL;
#ifdef DAIMA_AGENT_ROLES_ENABLED
        system_prompt[0] = '\0';
        if (role_count > 0) {
            agent_loop_append_role_prompt(system_prompt, DAIMA_CONTEXT_BUF_SIZE, active_role);
        }
#endif
        err = agent_turn_prepare(&msg,
#ifdef DAIMA_PLAN_REVIEW_ENABLED
                                 &plan,
#else
                                 NULL,
#endif
                                  system_prompt, DAIMA_CONTEXT_BUF_SIZE,
                                  history_json, DAIMA_LLM_STREAM_BUF_SIZE,
                                  &messages);

        char *final_text = NULL;
        char *reasoning_text = NULL;
        int iteration = 0;
        bool tool_budget_exhausted = false;
        bool cancelled = false;
        if (err == DAIMA_OK) {
            const char *tools_json = tool_registry_get_tools_json_for_channel(msg.channel);
#ifdef DAIMA_AGENT_COORDINATOR_ENABLED
            coordinator_t coord;
            memset(&coord, 0, sizeof(coord));
            daima_err_t coord_err = coordinator_decompose(msg.intent,
#ifdef DAIMA_PLAN_REVIEW_ENABLED
                                                          &plan,
#else
                                                          NULL,
#endif
                                                          msg.content,
                                                          &coord);
            if (coord_err == DAIMA_OK && coord.agent_count > 1) {
                DAIMA_LOGI(TAG, "Coordinator: launching %d sub-agents for intent=%s",
                           coord.agent_count, daima_intent_name(msg.intent));
                daima_err_t launch_err = coordinator_launch_all(system_prompt, messages, tools_json, &coord);
                if (launch_err == DAIMA_OK) {
                    coordinator_wait_all(&coord, 120000);
                    char *merged = daima_calloc(1, COORDINATOR_RESULT_MAX * COORDINATOR_MAX_SUB_AGENTS);
                    if (merged) {
                        coordinator_merge_results(&coord,
                                                  merged,
                                                  COORDINATOR_RESULT_MAX * COORDINATOR_MAX_SUB_AGENTS);
                        if (merged[0] != '\0') {
                            final_text = merged;
                            merged = NULL;
                        }
                        free(merged);
                    } else {
                        err = DAIMA_ERR_NO_MEM;
                    }
                } else {
                    DAIMA_LOGW(TAG, "Coordinator launch skipped: %s", daima_err_to_name(launch_err));
                }
                coordinator_free(&coord);
                cJSON_Delete(messages);
                agent_turn_finish(&msg, &final_text, &reasoning_text, err, iteration,
                                  tool_budget_exhausted, cancelled);
                continue;
            } else if (coord_err != DAIMA_OK) {
                DAIMA_LOGW(TAG, "Coordinator skipped: %s", daima_err_to_name(coord_err));
            }
            coordinator_free(&coord);
#endif
#ifdef DAIMA_TEAM_MODE_ENABLED
#ifdef DAIMA_PLAN_REVIEW_ENABLED
            team_orchestrator_t team = {0};
            if (plan.has_plan && plan.reviewed) {
                daima_err_t team_err = team_mode_orchestrate(&plan, system_prompt, tools_json, &team);
                if (team_err == DAIMA_OK && team.completed_count > 0) {
                    daima_err_t inject_err = team_mode_inject_to_prompt(&team, system_prompt, DAIMA_CONTEXT_BUF_SIZE);
                    if (inject_err == DAIMA_OK) {
                        DAIMA_LOGI(TAG, "Team Mode guidance injected: sub_agents=%d timeout_ms=%d",
                                   team.max_sub_agents, team.sub_agent_timeout_ms);
                    } else {
                        DAIMA_LOGW(TAG, "Team Mode prompt injection skipped: %s", daima_err_to_name(inject_err));
                    }
                } else if (team_err != DAIMA_OK) {
                    DAIMA_LOGW(TAG, "Team Mode orchestration skipped: %s", daima_err_to_name(team_err));
                }
            }
#endif
#endif
            err = agent_turn_run(system_prompt, messages, tools_json, &msg,
                                 cancel_token,
                                 &final_text, &reasoning_text, &iteration, &tool_budget_exhausted, &cancelled);
        }

        cJSON_Delete(messages);
        agent_turn_finish(&msg, &final_text, &reasoning_text, err, iteration, tool_budget_exhausted, cancelled);
    }
}

daima_err_t agent_loop_init(void)
{
    daima_err_t err = context_compressor_init();
    if (err != DAIMA_OK) {
        return err;
    }
    if (runtime_config_get_learning_review_enabled()) {
        err = learning_review_init();
        if (err != DAIMA_OK) {
            return err;
        }
    } else {
        DAIMA_LOGI(TAG, "Learning review disabled");
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
