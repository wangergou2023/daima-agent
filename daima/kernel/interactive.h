#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"
#include "err.h"

daima_err_t channel_runtime_request_sudo(const daima_msg_t *msg,
                                        const char *request_id,
                                        const char *prompt_text);
bool channel_runtime_wait_sudo_password(const daima_msg_t *msg,
                                        const char *request_id,
                                        char *password_out,
                                        size_t password_out_size);
