#pragma once

#include <stdbool.h>
#include <sys/select.h>

#include "daima_err.h"

typedef struct cJSON cJSON;

void ws_client_session_init(void);
bool ws_client_session_add(int fd);
int ws_client_session_update_fdset(fd_set *readfds, int maxfd);
void ws_client_session_handle_ready(const fd_set *readfds);
void ws_client_session_keepalive_tick(void);
daima_err_t ws_client_session_send_json(const char *chat_id, cJSON *obj);
