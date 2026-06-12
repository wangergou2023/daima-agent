#include "agent/agent_status_push.h"

#include "app/runtime_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *agent_status_push_intent_payload(daima_intent_t intent)
{
    switch (intent) {
    case DAIMA_INTENT_QA:
        return "qa";
    case DAIMA_INTENT_IMPLEMENT:
        return "implement";
    case DAIMA_INTENT_INVESTIGATE:
        return "investigate";
    case DAIMA_INTENT_FIX:
        return "fix";
    case DAIMA_INTENT_OPEN:
        return "open";
    case DAIMA_INTENT_COUNT:
    default:
        return "unknown";
    }
}

static const char *agent_status_push_role_payload(agent_role_t role)
{
    switch (role) {
    case AGENT_ROLE_PLANNER:
        return "planner";
    case AGENT_ROLE_EXECUTOR:
        return "executor";
    case AGENT_ROLE_REVIEWER:
        return "reviewer";
    case AGENT_ROLE_FAST:
        return "fast";
    case AGENT_ROLE_COUNT:
    default:
        return "";
    }
}

static daima_err_t agent_status_push_json(const daima_msg_t *msg, const char *json)
{
    if (!msg || !json) {
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_msg_t out = {0};
    strncpy(out.channel, msg->channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, msg->chat_id, sizeof(out.chat_id) - 1);
    out.content = strdup(json);
    if (!out.content) {
        return DAIMA_ERR_NO_MEM;
    }

    daima_err_t err = message_bus_push_outbound(&out);
    if (err != DAIMA_OK) {
        free(out.content);
    }
    return err;
}

static const char *agent_status_push_model_for_intent(daima_intent_t intent)
{
    const daima_category_profile_t *profile = category_router_resolve(intent);
    if (profile && profile->model[0]) {
        return profile->model;
    }
    return runtime_config_get_provider_model();
}

static daima_err_t agent_status_push_agent_state_role(const daima_msg_t *msg,
                                                      daima_intent_t intent,
                                                      const char *role)
{
    char state_json[512];
    int written = snprintf(state_json,
                           sizeof(state_json),
                           "{\"type\":\"agent_state\",\"intent\":\"%s\",\"role\":\"%s\",\"model\":\"%s\"}",
                           agent_status_push_intent_payload(intent),
                           role ? role : "",
                           agent_status_push_model_for_intent(intent));
    if (written < 0 || (size_t)written >= sizeof(state_json)) {
        return DAIMA_ERR_NO_MEM;
    }
    return agent_status_push_json(msg, state_json);
}

daima_err_t agent_status_push_agent_state(const daima_msg_t *msg,
                                          daima_intent_t intent,
                                          agent_role_t role)
{
    return agent_status_push_agent_state_role(msg, intent, agent_status_push_role_payload(role));
}

daima_err_t agent_status_push_agent_state_clear(const daima_msg_t *msg,
                                                daima_intent_t intent)
{
    return agent_status_push_agent_state_role(msg, intent, "");
}

static const char *agent_status_push_agent_status(const sub_agent_t *agent)
{
    if (!agent || !agent->done) {
        return "running";
    }
    return agent->error == DAIMA_OK ? "done" : "error";
}

static const char *agent_status_push_agent_detail(const sub_agent_t *agent)
{
    if (!agent || !agent->done) {
        return "启动中...";
    }
    return agent->error == DAIMA_OK ? "已完成" : "执行失败";
}

daima_err_t agent_status_push_coordinator_status(const daima_msg_t *msg,
                                                 const coordinator_t *coord)
{
    if (!coord || coord->agent_count <= 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char status_json[2048];
    int off = snprintf(status_json, sizeof(status_json),
                       "{\"type\":\"coordinator_status\",\"agents\":[");
    if (off < 0 || (size_t)off >= sizeof(status_json)) {
        return DAIMA_ERR_NO_MEM;
    }

    for (int i = 0; i < coord->agent_count && i < COORDINATOR_MAX_SUB_AGENTS; i++) {
        const sub_agent_t *agent = &coord->agents[i];
        int written = snprintf(status_json + off,
                               sizeof(status_json) - (size_t)off,
                               "%s{\"name\":\"%s\",\"status\":\"%s\",\"detail\":\"%s\"}",
                               i > 0 ? "," : "",
                               agent_role_name(agent->role),
                               agent_status_push_agent_status(agent),
                               agent_status_push_agent_detail(agent));
        if (written < 0 || (size_t)written >= sizeof(status_json) - (size_t)off) {
            return DAIMA_ERR_NO_MEM;
        }
        off += written;
    }

    int written = snprintf(status_json + off, sizeof(status_json) - (size_t)off, "]}");
    if (written < 0 || (size_t)written >= sizeof(status_json) - (size_t)off) {
        return DAIMA_ERR_NO_MEM;
    }

    return agent_status_push_json(msg, status_json);
}
