#include "drivers/tool/tool_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "interactive.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/tool/tool_registry.h"
#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"
static void log_tool_runtime_input(const char *phase,
                                   const char *tool_name,
                                   const char *input_json)
{
    char preview[320];
    const char *src = input_json ? input_json : "<null>";
    size_t n = strlen(src);
    size_t shown = n > sizeof(preview) - 1 ? sizeof(preview) - 1 : n;
    memcpy(preview, src, shown);
    preview[shown] = '\0';
    for (size_t i = 0; i < shown; i++) {
        if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
            preview[i] = ' ';
        }
    }
    pr_info("%s tool=%s input_len=%u input=%s%s", phase, tool_name && tool_name[0] ? tool_name : "<missing>", (unsigned)n, preview, n > shown ? "..." : "");
}

static void log_tool_runtime_result(const char *tool_name,
                                    const char *input_json,
                                    const char *output,
                                    err_t err,
                                    long elapsed_ms)
{
    char input_preview[240];
    char output_preview[240];
    const char *in = input_json ? input_json : "";
    const char *out = output ? output : "";
    size_t in_len = strlen(in);
    size_t out_len = strlen(out);
    size_t in_shown = in_len > sizeof(input_preview) - 1 ? sizeof(input_preview) - 1 : in_len;
    size_t out_shown = out_len > sizeof(output_preview) - 1 ? sizeof(output_preview) - 1 : out_len;
    memcpy(input_preview, in, in_shown);
    input_preview[in_shown] = '\0';
    memcpy(output_preview, out, out_shown);
    output_preview[out_shown] = '\0';
    for (size_t i = 0; i < in_shown; i++) {
        if (input_preview[i] == '\n' || input_preview[i] == '\r' || input_preview[i] == '\t') input_preview[i] = ' ';
    }
    for (size_t i = 0; i < out_shown; i++) {
        if (output_preview[i] == '\n' || output_preview[i] == '\r' || output_preview[i] == '\t') output_preview[i] = ' ';
    }
    pr_info("execute result tool=%s err=%s elapsed_ms=%ld input_len=%u input=%s output_len=%u output=%s", tool_name && tool_name[0] ? tool_name : "<missing>", err_name(err), elapsed_ms, (unsigned)in_len, input_preview[0] ? input_preview : "<empty>", (unsigned)out_len, output_preview[0] ? output_preview : "<empty>");
}

static void maybe_retry_terminal_with_web_sudo(const llm_tool_call_t *call,
                                               const struct message *msg,
                                               char *tool_output,
                                               size_t tool_output_size)
{
    if (!call || !msg || !tool_output || strcmp(call->name, "terminal") != 0) {
        return;
    }
    if (strcmp(msg->channel, CHAN_WEBSOCKET) != 0) {
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

    pr_info("Received sudo password from web, retrying terminal command");

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
    kfree(retry_input);
    cJSON_Delete(root);
}

err_t tool_runtime_execute_call(const llm_tool_call_t *call,
                                     const struct message *msg,
                                     char *tool_output,
                                     size_t tool_output_size,
                                     tool_runtime_result_t *out_result)
{
    if (!call || !msg || !tool_output || tool_output_size == 0 || !out_result) {
        return ERR_INVALID_ARG;
    }

    memset(out_result, 0, sizeof(*out_result));
    const char *tool_input = call->input ? call->input : "{}";
    log_tool_runtime_input("execute original input",
                           call->name,
                           call->input ? call->input : "<null>");
    char *patched_input = tool_invocation_context_patch_input(call, msg);
    if (patched_input) {
        tool_input = patched_input;
        log_tool_runtime_input("execute patched input", call->name, tool_input);
    }

    struct timespec started = {0};
    struct timespec ended = {0};
    clock_gettime(CLOCK_MONOTONIC, &started);
    tool_output[0] = '\0';
    err_t exec_err = tool_registry_execute_for_channel(msg->channel, call->name, tool_input, tool_output, tool_output_size);
    maybe_retry_terminal_with_web_sudo(call, msg, tool_output, tool_output_size);
    clock_gettime(CLOCK_MONOTONIC, &ended);

    out_result->elapsed_ms = (ended.tv_sec - started.tv_sec) * 1000L
                           + (ended.tv_nsec - started.tv_nsec) / 1000000L;
    out_result->effective_input = patched_input;
    log_tool_runtime_result(call->name, tool_input, tool_output, exec_err, out_result->elapsed_ms);
    return exec_err;
}
