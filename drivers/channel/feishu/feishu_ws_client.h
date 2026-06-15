/* 飞书 WebSocket 连接与帧编解码辅助。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

#define FEISHU_WS_MAX_HEADERS 16

#define FEISHU_WS_OPCODE_CONT   0x0
#define FEISHU_WS_OPCODE_TEXT   0x1
#define FEISHU_WS_OPCODE_BINARY 0x2
#define FEISHU_WS_OPCODE_CLOSE  0x8
#define FEISHU_WS_OPCODE_PING   0x9
#define FEISHU_WS_OPCODE_PONG   0xA

typedef struct {
    int sock;
    bool tls;
    SSL_CTX *ctx;
    SSL *ssl;
} feishu_ws_conn_t;

typedef struct {
    char key[32];
    char value[128];
} feishu_ws_header_t;

typedef struct {
    uint64_t seq_id;
    uint64_t log_id;
    int32_t service;
    int32_t method;
    feishu_ws_header_t headers[FEISHU_WS_MAX_HEADERS];
    size_t header_count;
    const uint8_t *payload;
    size_t payload_len;
} feishu_ws_frame_t;

void feishu_ws_conn_init(feishu_ws_conn_t *conn);
void feishu_ws_conn_close(feishu_ws_conn_t *conn);

int feishu_ws_wait_readable(feishu_ws_conn_t *conn, int timeout_ms);
bool feishu_ws_connect(const char *url, feishu_ws_conn_t *conn);
bool feishu_ws_send_frame_raw(feishu_ws_conn_t *conn, int opcode, const uint8_t *payload, size_t payload_len);
bool feishu_ws_read_frame(feishu_ws_conn_t *conn, int *out_opcode, uint8_t **out_payload, size_t *out_len);

bool feishu_ws_parse_frame(const uint8_t *buf, size_t len, feishu_ws_frame_t *frame);
const char *feishu_ws_frame_header_value(const feishu_ws_frame_t *frame, const char *key);
int feishu_ws_send_frame(feishu_ws_conn_t *conn,
                         const feishu_ws_frame_t *frame,
                         const uint8_t *payload,
                         size_t payload_len);
