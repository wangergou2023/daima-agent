/* 本地 terminal 工具实现：执行 shell 命令并返回结构化结果。 */

#include "drivers/tool/tool_system.h"
#include "drivers/tool/tool_terminal_exec.h"

#include "fs.h"
#include "paths.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "cJSON.h"

static const char *TAG = "tool_terminal";

#define TERMINAL_DEFAULT_TIMEOUT 120
#define TERMINAL_MAX_TIMEOUT     1800
#define SUDO_ENV_NAME            "DAIMA_SUDO_PASSWORD"

static const daima_tool_t s_terminal_tool = {
    .name = "terminal",
    .description = "执行本地 shell 命令并返回结构化结果。默认 workdir 是 Daima 自己的 workspace，不是启动目录；临时脚本、生成文件和 npm install 等依赖安装默认应留在该 workspace。只有需要操作明确项目时才传入项目 workdir。返回 JSON 字符串，包含 output、exit_code、timed_out、workdir。对于 apt-get/apt/yum/dnf/pip install/npm install 等安装、更新命令，请显式设置更长 timeout（如 300 或 600 秒），避免默认 120 秒超时。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"command\":{\"type\":\"string\",\"description\":\"要执行的 shell 命令\"},"
        "\"cmd\":{\"type\":\"string\",\"description\":\"兼容字段（同 command）\"},"
        "\"timeout\":{\"type\":\"integer\",\"description\":\"超时秒数，默认 120。安装软件、更新包索引、构建大项目时建议设置为 300 或 600\"},"
        "\"workdir\":{\"type\":\"string\",\"description\":\"可选工作目录；为空则使用 Daima workspace。只有明确要操作某个项目时才传项目路径\"}"
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

static bool command_contains_any(const char *command, const char *const *needles, size_t count)
{
    if (!command) return false;
    for (size_t i = 0; i < count; ++i) {
        if (strstr(command, needles[i])) {
            return true;
        }
    }
    return false;
}

static bool command_contains_sensitive_path(const char *command)
{
    static const char *const sensitive[] = {
        "~/.ssh",
        "/.ssh/",
        ".env",
        "id_rsa",
        "id_ed25519",
        "/etc/shadow",
        "/etc/sudoers",
        "config/config.json",
    };
    return command_contains_any(command, sensitive, sizeof(sensitive) / sizeof(sensitive[0]));
}

static bool command_is_destructive(const char *command)
{
    if (!command) return false;
    static const char *const dangerous[] = {
        "rm -rf /",
        "rm -fr /",
        "mkfs.",
        "dd if=",
        "dd of=",
        ":(){",
        "chmod -R 777 /",
        "chown -R ",
    };
    return command_contains_any(command, dangerous, sizeof(dangerous) / sizeof(dangerous[0]));
}

static bool command_uses_blocked_network_tool(const char *command)
{
    if (!command) return false;
    static const char *const network_tools[] = {
        "nc ",
        "ncat ",
        "telnet ",
        "ssh ",
        "scp ",
    };
    return command_contains_any(command, network_tools, sizeof(network_tools) / sizeof(network_tools[0]));
}

static bool command_uses_inline_code(const char *command)
{
    if (!command) return false;
    static const char *const inline_code[] = {
        "node -e",
        "node --eval",
        "python -c",
        "python3 -c",
        "perl -e",
        "ruby -e",
    };
    return command_contains_any(command, inline_code, sizeof(inline_code) / sizeof(inline_code[0]));
}

static bool command_pipes_remote_shell(const char *command)
{
    if (!command) return false;
    return (strstr(command, "curl ") || strstr(command, "wget ")) &&
           (strstr(command, "| sh") || strstr(command, "| bash") || strstr(command, "| sudo sh") || strstr(command, "| sudo bash"));
}

static bool terminal_command_allowed(const char *command, const char **reason)
{
    if (!command || !command[0]) {
        if (reason) *reason = "missing_command";
        return false;
    }

    const char *level = runtime_config_get_terminal_security_level();
    bool build_mode = strcmp(level, "build") == 0;

    if (command_is_destructive(command)) {
        if (reason) *reason = "dangerous_command_blocked";
        return false;
    }
    if (command_contains_sensitive_path(command)) {
        if (reason) *reason = "sensitive_path_blocked";
        return false;
    }
    if (command_uses_blocked_network_tool(command)) {
        if (reason) *reason = "network_command_blocked";
        return false;
    }
    if (!build_mode && command_uses_inline_code(command)) {
        if (reason) *reason = "inline_code_blocked";
        return false;
    }
    if (!build_mode && (strstr(command, "$(") || strchr(command, '`'))) {
        if (reason) *reason = "shell_expansion_blocked";
        return false;
    }
    if (command_pipes_remote_shell(command)) {
        if (reason) *reason = "remote_shell_pipe_blocked";
        return false;
    }
    return true;
}

static void write_blocked_terminal_result(char *output,
                                          size_t output_size,
                                          const char *command,
                                          const char *workdir,
                                          const char *reason)
{
    terminal_exec_result_t blocked = {
        .exit_code = 126,
        .timed_out = false,
        .truncated = false,
        .signal_num = 0,
        .output = "",
    };
    char *json = terminal_json_result_string(command, workdir ? workdir : "", &blocked, reason);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON_AddStringToObject(
                root,
                "message",
                "命令被安全策略拦截。下一步不要使用 node -e/python -c/内联代码；请先用 apply_patch 新建真实脚本文件，再用 terminal 执行 {\"command\":\"node script.js\"}。");
            char *with_message = cJSON_PrintUnformatted(root);
            if (with_message) {
                strncpy(output, with_message, output_size - 1);
                output[output_size - 1] = '\0';
                free(with_message);
            } else {
                strncpy(output, json, output_size - 1);
                output[output_size - 1] = '\0';
            }
            cJSON_Delete(root);
        } else {
            strncpy(output, json, output_size - 1);
            output[output_size - 1] = '\0';
        }
        free(json);
    } else {
        snprintf(output, output_size, "{\"error\":\"%s\"}", reason ? reason : "command_blocked");
    }
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
    const char *effective_workdir = (workdir && workdir[0]) ? workdir : daima_path_workspace_dir();
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
    const char *blocked_reason = NULL;
    if (!terminal_command_allowed(command, &blocked_reason)) {
        write_blocked_terminal_result(output,
                                      output_size,
                                      command,
                                      effective_workdir,
                                      blocked_reason ? blocked_reason : "command_blocked");
        DAIMA_LOGW(TAG, "terminal command blocked: reason=%s cmd=%.120s",
                  blocked_reason ? blocked_reason : "command_blocked", command);
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_STATE;
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
            char *json = terminal_json_status_string(command, effective_workdir, "sudo_password_required", message, request_id);
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
    daima_fs_ensure_dir(effective_workdir);
    daima_err_t err = terminal_execute_local_shell(
        effective_command ? effective_command : command,
        effective_workdir,
        timeout_seconds,
        stdin_payload ? stdin_payload : NULL,
        output_size > 4096 ? (output_size / 2) : output_size,
        &result);

    char *json = NULL;
    if (err == DAIMA_OK) {
        json = terminal_json_result_string(command, effective_workdir, &result, "");
    } else {
        terminal_exec_result_t empty = { .exit_code = -1, .timed_out = false, .truncated = false, .signal_num = 0, .output = "" };
        json = terminal_json_result_string(command, effective_workdir, &empty, "execution_failed");
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
            effective_workdir);
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
