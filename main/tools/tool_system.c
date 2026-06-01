/* 本地 terminal 工具实现：执行 shell 命令并返回结构化结果。 */

#include "tools/tool_system.h"
#include "tools/tool_terminal_exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "daima_log.h"
#include "cJSON.h"

static const char *TAG = "tool_terminal";

#define TERMINAL_DEFAULT_TIMEOUT 120
#define TERMINAL_MAX_TIMEOUT     1800
#define SUDO_ENV_NAME            "DAIMA_SUDO_PASSWORD"

static const daima_tool_t s_terminal_tool = {
    .name = "terminal",
    .description = "执行本地 shell 命令并返回结构化结果。适合安装软件、运行构建、包管理、git、查看进程。返回 JSON 字符串，包含 output、exit_code、timed_out、workdir。对于 apt-get/apt/yum/dnf/pip install/npm install 等安装、更新命令，请显式设置更长 timeout（如 300 或 600 秒），避免默认 120 秒超时。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"command\":{\"type\":\"string\",\"description\":\"要执行的 shell 命令\"},"
        "\"cmd\":{\"type\":\"string\",\"description\":\"兼容字段（同 command）\"},"
        "\"timeout\":{\"type\":\"integer\",\"description\":\"超时秒数，默认 120。安装软件、更新包索引、构建大项目时建议设置为 300 或 600\"},"
        "\"workdir\":{\"type\":\"string\",\"description\":\"可选工作目录；为空则使用进程当前目录\"}"
        "},"
        "\"required\":[\"command\"]}",
    .execute = tool_terminal_execute,
};

static int json_get_int_or_default(cJSON *root, const char *key, int fallback)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!item || !cJSON_IsNumber(item)) {
        return fallback;
    }
    return item->valueint;
}

daima_err_t tool_terminal_execute(const char *input_json, char *output, size_t output_size)
{
    if (!output || output_size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\":\"invalid_input_json\"}");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "command"));
    if (!command || !command[0]) {
        command = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
    }
    const char *workdir = cJSON_GetStringValue(cJSON_GetObjectItem(root, "workdir"));
    const char *inline_sudo_password = cJSON_GetStringValue(cJSON_GetObjectItem(root, "sudo_password"));
    int timeout_seconds = json_get_int_or_default(root, "timeout", TERMINAL_DEFAULT_TIMEOUT);

    if (!command || !command[0]) {
        snprintf(output, output_size, "{\"error\":\"missing_command\"}");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (strlen(command) > 4000) {
        snprintf(output, output_size, "{\"error\":\"command_too_long\"}");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (timeout_seconds < 1) timeout_seconds = TERMINAL_DEFAULT_TIMEOUT;
    if (timeout_seconds > TERMINAL_MAX_TIMEOUT) timeout_seconds = TERMINAL_MAX_TIMEOUT;

    char *effective_command = NULL;
    char *stdin_payload = NULL;
    const char *sudo_password = (inline_sudo_password && inline_sudo_password[0])
        ? inline_sudo_password
        : getenv(SUDO_ENV_NAME);
    if (terminal_command_has_real_sudo(command)) {
        if (!sudo_password || !sudo_password[0]) {
            char request_id[48];
            snprintf(request_id, sizeof(request_id), "sudo_%ld_%d", (long)time(NULL), (int)getpid());
            char message[192];
            snprintf(message, sizeof(message), "command uses sudo and needs a password; set %s or provide it interactively", SUDO_ENV_NAME);
            char *json = terminal_json_status_string(command, workdir ? workdir : "", "sudo_password_required", message, request_id);
            if (json) {
                strncpy(output, json, output_size - 1);
                output[output_size - 1] = '\0';
                free(json);
            } else {
                snprintf(output, output_size, "{\"status\":\"sudo_password_required\"}");
            }
            cJSON_Delete(root);
            return DAIMA_OK;
        }
        effective_command = terminal_rewrite_sudo_command(command);
        if (!effective_command) {
            snprintf(output, output_size, "{\"error\":\"sudo_rewrite_failed\"}");
            cJSON_Delete(root);
            return DAIMA_ERR_NO_MEM;
        }
        size_t pw_len = strlen(sudo_password);
        stdin_payload = calloc(1, pw_len + 2);
        if (!stdin_payload) {
            free(effective_command);
            snprintf(output, output_size, "{\"error\":\"sudo_password_buffer_failed\"}");
            cJSON_Delete(root);
            return DAIMA_ERR_NO_MEM;
        }
        memcpy(stdin_payload, sudo_password, pw_len);
        stdin_payload[pw_len] = '\n';
    }

    terminal_exec_result_t result;
    memset(&result, 0, sizeof(result));
    daima_err_t err = terminal_execute_local_shell(
        effective_command ? effective_command : command,
        workdir,
        timeout_seconds,
        stdin_payload ? stdin_payload : NULL,
        output_size > 4096 ? (output_size / 2) : output_size,
        &result);

    char *json = NULL;
    if (err == DAIMA_OK) {
        json = terminal_json_result_string(command, workdir ? workdir : "", &result, "");
    } else {
        terminal_exec_result_t empty = { .exit_code = -1, .timed_out = false, .truncated = false, .signal_num = 0, .output = "" };
        json = terminal_json_result_string(command, workdir ? workdir : "", &empty, "execution_failed");
    }

    if (json) {
        strncpy(output, json, output_size - 1);
        output[output_size - 1] = '\0';
        free(json);
    } else {
        snprintf(output, output_size, "{\"error\":\"result_encode_failed\"}");
        err = DAIMA_ERR_NO_MEM;
    }

    if (err == DAIMA_OK) {
        DAIMA_LOGI(
            TAG,
            "terminal: cmd=%.120s exit=%d timeout=%d workdir=%s",
            command,
            result.exit_code,
            timeout_seconds,
            (workdir && workdir[0]) ? workdir : "(default)");
    }

    free(stdin_payload);
    free(effective_command);
    free(result.output);
    cJSON_Delete(root);
    return err;
}

const daima_tool_t *tool_terminal_definition(void)
{
    return &s_terminal_tool;
}
