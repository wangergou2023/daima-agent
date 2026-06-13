/* terminal 执行辅助层。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "core/err.h"

typedef struct {
    int exit_code;
    bool timed_out;
    bool truncated;
    int signal_num;
    char *output;
} terminal_exec_result_t;

bool terminal_command_has_real_sudo(const char *command);
char *terminal_rewrite_sudo_command(const char *command);

char *terminal_json_result_string(const char *command,
                                  const char *workdir,
                                  const terminal_exec_result_t *result,
                                  const char *error_text);

char *terminal_json_status_string(const char *command,
                                  const char *workdir,
                                  const char *status,
                                  const char *message,
                                  const char *request_id);

daima_err_t terminal_execute_local_shell(const char *command,
                                        const char *workdir,
                                        int timeout_seconds,
                                        const char *stdin_data,
                                        size_t output_cap,
                                        terminal_exec_result_t *out);
