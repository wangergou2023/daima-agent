#pragma once

#include "agent/intent_gate.h"
#include "agent/plan_review.h"
#include "agent/agent_roles.h"
#include "llm/llm_proxy.h"
#include "daima_err.h"

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#define COORDINATOR_MAX_SUB_AGENTS 4
#define COORDINATOR_RESULT_MAX 4096

typedef struct {
    agent_role_t role;
    char system_prompt_add[1024];
    char task_description[512];
    char result_text[COORDINATOR_RESULT_MAX];
    bool done;
    daima_err_t error;
    llm_async_chat_t *async_chat;
    cJSON *scoped_messages;
} sub_agent_t;

typedef struct {
    sub_agent_t agents[COORDINATOR_MAX_SUB_AGENTS];
    int agent_count;
    char merged_result[16384];
} coordinator_t;

daima_err_t coordinator_decompose(daima_intent_t intent,
                                   const daima_plan_t *plan,
                                   const char *user_message,
                                   coordinator_t *out);
daima_err_t coordinator_merge_results(coordinator_t *coord,
                                        char *output, size_t output_size);
daima_err_t coordinator_launch_all(const char *base_system_prompt,
                                   cJSON *shared_messages,
                                   const char *tools_json,
                                   coordinator_t *coord);
daima_err_t coordinator_wait_all(coordinator_t *coord, int timeout_ms);
void coordinator_free(coordinator_t *coord);
