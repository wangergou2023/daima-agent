#include "drivers/channel/feishu/feishu_ws_runtime.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "drivers/channel/feishu/feishu_api.h"
#include "drivers/channel/feishu/feishu_event_handler.h"
#include "log.h"
#include "os.h"
#include "text.h"
#include "cJSON.h"

static const char *TAG = "feishu";

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

void feishu_ws_runtime_init(feishu_ws_runtime_t *rt)
{
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
    rt->ping_interval_ms = 120000;
    rt->reconnect_interval_ms = 30000;
    rt->reconnect_nonce_ms = 30000;
}

static void feishu_handle_ws_frame(feishu_ws_conn_t *conn,
                                   const char *app_id,
                                   const char *app_secret,
                                   feishu_ws_runtime_t *rt,
                                   const uint8_t *buf,
                                   size_t len)
{
    feishu_ws_frame_t frame = {0};
    if (!feishu_ws_parse_frame(buf, len, &frame)) {
        DAIMA_LOGW(TAG, "WS frame parse failed");
        return;
    }

    const char *type = feishu_ws_frame_header_value(&frame, "type");
    if (frame.method == 0) {
        if (type && strcmp(type, "pong") == 0 && frame.payload && frame.payload_len > 0) {
            cJSON *cfg = cJSON_ParseWithLength((const char *)frame.payload, frame.payload_len);
            if (cfg) {
                cJSON *pi = cJSON_GetObjectItem(cfg, "PingInterval");
                if (pi && cJSON_IsNumber(pi)) rt->ping_interval_ms = pi->valueint * 1000;
                cJSON_Delete(cfg);
            }
        }
        return;
    }
    if (!type || strcmp(type, "event") != 0) return;
    if (!frame.payload || frame.payload_len == 0) return;

    feishu_event_handler_process_ws_event_json(
        app_id, app_secret,
        (const char *)frame.payload, frame.payload_len);

    char ack[32];
    int ack_len = snprintf(ack, sizeof(ack), "{\"code\":200}");
    if (conn) {
        feishu_ws_send_frame(conn, &frame, (const uint8_t *)ack, (size_t)ack_len);
    }
}

void feishu_ws_runtime_run(feishu_ws_runtime_t *rt,
                           const char *app_id,
                           const char *app_secret)
{
    if (!rt || !app_id || !app_id[0] || !app_secret || !app_secret[0]) {
        return;
    }

    feishu_ws_conn_t conn;
    feishu_ws_conn_init(&conn);

    while (1) {
        feishu_ws_config_t cfg = {0};
        if (feishu_api_pull_ws_config(app_id, app_secret, &cfg) != DAIMA_OK) {
            daima_task_delay(5000);
            continue;
        }

        daima_safe_copy(rt->url, sizeof(rt->url), cfg.url);
        rt->service_id = cfg.service_id;
        rt->ping_interval_ms = cfg.ping_interval_ms;
        rt->reconnect_interval_ms = cfg.reconnect_interval_ms;
        rt->reconnect_nonce_ms = cfg.reconnect_nonce_ms;

        if (!feishu_ws_connect(rt->url, &conn)) {
            int wait_ms = rt->reconnect_interval_ms;
            if (rt->reconnect_nonce_ms > 0) {
                wait_ms += (rand() % rt->reconnect_nonce_ms);
            }
            daima_task_delay((uint32_t)wait_ms);
            continue;
        }

        rt->connected = true;
        DAIMA_LOGI(TAG, "Feishu WS connected");

        int64_t last_ping = 0;
        while (1) {
            int64_t now = now_ms();
            int64_t due = rt->ping_interval_ms - (now - last_ping);
            int timeout = 200;
            if (due < timeout) timeout = (int)(due < 0 ? 0 : due);

            int ready = feishu_ws_wait_readable(&conn, timeout);
            if (ready < 0) {
                DAIMA_LOGW(TAG, "WS read error");
                break;
            }
            if (ready > 0) {
                int opcode = 0;
                uint8_t *payload = NULL;
                size_t payload_len = 0;
                if (!feishu_ws_read_frame(&conn, &opcode, &payload, &payload_len)) {
                    DAIMA_LOGW(TAG, "WS frame read failed");
                    free(payload);
                    break;
                }
                if (opcode == FEISHU_WS_OPCODE_PING) {
                    feishu_ws_send_frame_raw(&conn, FEISHU_WS_OPCODE_PONG, payload, payload_len);
                } else if (opcode == FEISHU_WS_OPCODE_BINARY) {
                    feishu_handle_ws_frame(&conn, app_id, app_secret, rt, payload, payload_len);
                } else if (opcode == FEISHU_WS_OPCODE_CLOSE) {
                    free(payload);
                    break;
                }
                free(payload);
            }

            now = now_ms();
            if (rt->ping_interval_ms > 0 && now - last_ping >= rt->ping_interval_ms) {
                feishu_ws_frame_t ping = {0};
                ping.seq_id = 0;
                ping.log_id = 0;
                ping.service = rt->service_id;
                ping.method = 0;
                ping.header_count = 1;
                daima_safe_copy(ping.headers[0].key, sizeof(ping.headers[0].key), "type");
                daima_safe_copy(ping.headers[0].value, sizeof(ping.headers[0].value), "ping");
                feishu_ws_send_frame(&conn, &ping, NULL, 0);
                last_ping = now;
            }
        }

        feishu_ws_conn_close(&conn);
        rt->connected = false;
        DAIMA_LOGW(TAG, "Feishu WS disconnected");
        daima_task_delay(3000);
    }
}
