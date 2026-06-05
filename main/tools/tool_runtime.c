#include "tools/tool_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/interactive_requests.h"
#include "tools/tool_invocation_context.h"
#include "tools/tool_registry.h"
#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "tool_runtime";

static void maybe_retry_terminal_with_web_sudo(const llm_tool_call_t *call,
                                               const daima_msg_t *msg,
                                               char *tool_output,
                                               size_t tool_output_size)
{
    if (!call || !msg || !tool_output || strcmp(call->name, "terminal") != 0) {
        return;
    }
    if (strcmp(msg->channel, DAIMA_CHAN_WEBSOCKET) != 0) {
        return;
    }

    cJSON *root = cJSON_Parse(tool_output);
    if (!root) {
        return;
    }
    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(root, "status"));
    const char *request_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "request_id"));
    if (!status || strcmp(status, "sudo_password_required") != 0 || !request_id || !request_id[0]) {
        cJSON_Delete(root);
        return;
    }

    char sudo_password[256] = {0};
    if (!channel_runtime_wait_sudo_password(msg, request_id, sudo_password, sizeof(sudo_password))) {
        const char *json = "{\"command\":\"\",\"workdir\":\"\",\"exit_code\":1,\"timed_out\":false,\"truncated\":false,\"signal\":0,\"output\":\"sudo password was not provided\",\"error\":\"sudo_password_cancelled\"}";
        strncpy(tool_output, json, tool_output_size - 1);
        tool_output[tool_output_size - 1] = '\0';
        cJSON_Delete(root);
        return;
    }

    DAIMA_LOGI(TAG, "Received sudo password from web, retrying terminal command");

    cJSON *tool_args = cJSON_Parse(call->input ? call->input : "{}");
    if (!tool_args) {
        cJSON_Delete(root);
        return;
    }
    cJSON_DeleteItemFromObject(tool_args, "sudo_password");
    cJSON_AddStringToObject(tool_args, "sudo_password", sudo_password);
    char *retry_input = cJSON_PrintUnformatted(tool_args);
    cJSON_Delete(tool_args);
    if (!retry_input) {
        cJSON_Delete(root);
        return;
    }

    tool_output[0] = '\0';
    tool_registry_execute_for_channel(msg->channel, call->name, retry_input, tool_output, tool_output_size);
    free(retry_input);
    cJSON_Delete(root);
}

daima_err_t tool_runtime_execute_call(const llm_tool_call_t *call,
                                     const daima_msg_t *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     daima_tool_runtime_result_t *out_result)
{
    if (!call || !msg || !tool_output || tool_output_size == 0 || !out_result) {
        return DAIMA_ERR_INVALID_ARG;
    }

    memset(out_result, 0, sizeof(*out_result));
    const char *tool_input = call->input ? call->input : "{}";
    char *patched_input = tool_invocation_context_patch_input(call, msg);
    if (patched_input) {
        tool_input = patched_input;
    }

    struct timespec started = {0};
    struct timespec ended = {0};
    clock_gettime(CLOCK_MONOTONIC, &started);
    tool_output[0] = '\0';
    daima_err_t exec_err = tool_registry_execute_for_channel(msg->channel, call->name, tool_input, tool_output, tool_output_size);
    maybe_retry_terminal_with_web_sudo(call, msg, tool_output, tool_output_size);
    clock_gettime(CLOCK_MONOTONIC, &ended);

    out_result->elapsed_ms = (ended.tv_sec - started.tv_sec) * 1000L
                           + (ended.tv_nsec - started.tv_nsec) / 1000000L;
    out_result->effective_input = patched_input;
    return exec_err;
}
