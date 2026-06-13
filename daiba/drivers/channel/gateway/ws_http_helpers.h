#ifndef DAIMA_WS_HTTP_HELPERS_H
#define DAIMA_WS_HTTP_HELPERS_H

int ws_http_handle_request(int client_fd, const char *req, const char *ui_fallback_html);

#endif
