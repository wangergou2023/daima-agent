#pragma once

#include <stdbool.h>
#include <sys/select.h>

#include "err.h"

typedef struct cJSON cJSON;

#define WS_CLIENT_CHAT_ID_LEN 64

void ws_client_session_init(void);
bool ws_client_session_add(int fd);
int ws_client_session_update_fdset(fd_set *readfds, int maxfd);
void ws_client_session_handle_ready(const fd_set *readfds);
void ws_client_session_keepalive_tick(void);
err_t ws_client_session_send_json(const char *chat_id, cJSON *obj);
err_t ws_client_session_send_json_quiet(const char *chat_id, cJSON *obj);
bool ws_client_session_has_chat_id(const char *chat_id);
bool ws_client_session_bind_chat_id(int fd, const char *chat_id);
void ws_pending_save(const char *chat_id, const char *response_text);
void ws_client_session_try_deliver_pending(const char *chat_id);
bool ws_client_chat_id_roundtrip_for_test(const char *chat_id);
void *ws_client_find_session_for_test(int fd);
void ws_client_dispatch_text_frame_for_test(int fd, void *client, const char *payload, time_t now);
