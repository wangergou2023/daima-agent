/* 飞书 WebSocket 传输层：连接、TLS、握手与原始帧收发。 */

#include "drivers/channel/feishu/feishu_ws_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "tls.h"
#include "base64.h"
#include "autoconf.h"
#include "log.h"
#include "text.h"

static const char *TAG = "feishu_ws";

#define FEISHU_WS_MAX_PAYLOAD      (512 * 1024)
#define FEISHU_WS_READ_TIMEOUT     DAIMA_TIMEOUT_SHORT
#define FEISHU_WS_CONNECT_TIMEOUT  DAIMA_TIMEOUT_SHORT

typedef struct {
    char scheme[8];
    char host[256];
    int port;
    char path[512];
    bool tls;
} ws_url_t;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int ws_conn_read_some(feishu_ws_conn_t *conn, void *buf, size_t len, int timeout_ms)
{
    int ready = feishu_ws_wait_readable(conn, timeout_ms);
    if (ready <= 0) return ready;

    if (conn->tls) {
        int n = SSL_read(conn->ssl, buf, (int)len);
        if (n <= 0) return -1;
        return n;
    }

    int n = (int)recv(conn->sock, buf, len, 0);
    if (n <= 0) return -1;
    return n;
}

static int ws_conn_write_all(feishu_ws_conn_t *conn, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        int n = 0;
        if (conn->tls) {
            n = SSL_write(conn->ssl, (const uint8_t *)buf + off, (int)(len - off));
        } else {
            n = (int)send(conn->sock, (const uint8_t *)buf + off, len - off, 0);
        }
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return (int)off;
}

static int ws_read_exact(feishu_ws_conn_t *conn, uint8_t *buf, size_t len, int timeout_ms)
{
    size_t off = 0;
    int64_t start = now_ms();
    while (off < len) {
        int to = timeout_ms;
        if (timeout_ms >= 0) {
            int64_t elapsed = now_ms() - start;
            if (elapsed >= timeout_ms) return -1;
            to = timeout_ms - (int)elapsed;
        }
        int n = ws_conn_read_some(conn, buf + off, len - off, to);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return (int)off;
}

static bool ws_parse_url(const char *url, ws_url_t *out)
{
    if (!url || !out) return false;
    memset(out, 0, sizeof(*out));

    const char *sep = strstr(url, "://");
    if (!sep) return false;
    size_t scheme_len = (size_t)(sep - url);
    if (scheme_len >= sizeof(out->scheme)) return false;
    memcpy(out->scheme, url, scheme_len);
    out->scheme[scheme_len] = '\0';

    out->tls = (strcmp(out->scheme, "wss") == 0);
    int default_port = out->tls ? 443 : 80;

    const char *host_start = sep + 3;
    const char *path_start = strchr(host_start, '/');
    const char *query_start = strchr(host_start, '?');
    if (!path_start && query_start) {
        path_start = query_start;
    }
    const char *host_end = path_start ? path_start : (url + strlen(url));

    const char *port_sep = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (port_sep) {
        size_t host_len = (size_t)(port_sep - host_start);
        if (host_len >= sizeof(out->host)) return false;
        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';
        out->port = atoi(port_sep + 1);
    } else {
        size_t host_len = (size_t)(host_end - host_start);
        if (host_len >= sizeof(out->host)) return false;
        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';
        out->port = default_port;
    }

    if (path_start) {
        if (*path_start == '?') {
            snprintf(out->path, sizeof(out->path), "/%s", path_start);
        } else {
            daima_safe_copy(out->path, sizeof(out->path), path_start);
        }
    } else {
        strcpy(out->path, "/");
    }

    return out->host[0] != '\0';
}

static int tcp_connect_timeout(const char *host, int port, int timeout_ms)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) continue;

        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int ret = connect(sock, ai->ai_addr, ai->ai_addrlen);
        if (ret == 0) {
            fcntl(sock, F_SETFL, flags);
            break;
        }
        if (errno != EINPROGRESS) {
            close(sock);
            sock = -1;
            continue;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(sock + 1, NULL, &wfds, NULL, &tv);
        if (ret > 0 && FD_ISSET(sock, &wfds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                fcntl(sock, F_SETFL, flags);
                break;
            }
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}

static void ws_apply_ca(SSL_CTX *ctx)
{
    host_tls_apply_ssl_ctx_ca(ctx);
}

static bool ws_tls_handshake(feishu_ws_conn_t *conn, const char *host)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return false;

    ws_apply_ca(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        return false;
    }
    SSL_set_tlsext_host_name(ssl, host);
    SSL_set_fd(ssl, conn->sock);

    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return false;
    }

    conn->ctx = ctx;
    conn->ssl = ssl;
    return true;
}

static bool ws_build_key(char *out, size_t out_size)
{
    unsigned char rnd[16];
    if (RAND_bytes(rnd, sizeof(rnd)) != 1) {
        for (size_t i = 0; i < sizeof(rnd); i++) {
            rnd[i] = (unsigned char)(rand() & 0xFF);
        }
    }
    size_t b64_len = 0;
    char *b64 = daima_base64_encode_alloc(rnd, sizeof(rnd), &b64_len);
    if (!b64) return false;
    daima_safe_copy(out, out_size, b64);
    free(b64);
    return true;
}

static bool ws_read_http_header(feishu_ws_conn_t *conn, char *out, size_t out_size, int timeout_ms)
{
    size_t used = 0;
    int64_t start = now_ms();
    while (used < out_size - 1) {
        int to = timeout_ms;
        if (timeout_ms >= 0) {
            int64_t elapsed = now_ms() - start;
            if (elapsed >= timeout_ms) break;
            to = timeout_ms - (int)elapsed;
        }
        int n = ws_conn_read_some(conn, (uint8_t *)out + used, out_size - 1 - used, to);
        if (n <= 0) break;
        used += (size_t)n;
        out[used] = '\0';
        if (strstr(out, "\r\n\r\n")) return true;
    }
    return false;
}

static bool ws_find_header(const char *resp, const char *name, char *out, size_t out_size)
{
    const char *p = resp;
    while (p && *p) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) break;
        if (line_end == p) break;
        const char *colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon) {
            size_t name_len = (size_t)(colon - p);
            if (strlen(name) == name_len && strncasecmp(p, name, name_len) == 0) {
                const char *val = colon + 1;
                while (*val == ' ' || *val == '\t') val++;
                size_t val_len = (size_t)(line_end - val);
                size_t n = (val_len < out_size - 1) ? val_len : out_size - 1;
                memcpy(out, val, n);
                out[n] = '\0';
                return true;
            }
        }
        p = line_end + 2;
    }
    return false;
}

static bool ws_verify_accept(const char *key, const char *accept)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)buf, strlen(buf), sha);
    size_t b64_len = 0;
    char *b64 = daima_base64_encode_alloc(sha, sizeof(sha), &b64_len);
    if (!b64) return false;
    bool ok = (strcmp(b64, accept) == 0);
    free(b64);
    return ok;
}

static bool ws_handshake(feishu_ws_conn_t *conn, const ws_url_t *url)
{
    char key[64];
    if (!ws_build_key(key, sizeof(key))) return false;

    char host_hdr[300];
    if ((url->tls && url->port != 443) || (!url->tls && url->port != 80)) {
        snprintf(host_hdr, sizeof(host_hdr), "%s:%d", url->host, url->port);
    } else {
        daima_safe_copy(host_hdr, sizeof(host_hdr), url->host);
    }

    char req[1024];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     url->path, host_hdr, key);
    if (n <= 0 || (size_t)n >= sizeof(req)) return false;

    if (ws_conn_write_all(conn, req, (size_t)n) < 0) return false;

    char resp[4096];
    if (!ws_read_http_header(conn, resp, sizeof(resp), FEISHU_WS_CONNECT_TIMEOUT)) return false;

    if (strstr(resp, " 101 ") == NULL) {
        DAIMA_LOGE(TAG, "WebSocket upgrade failed: %.120s", resp);
        return false;
    }

    char accept[128] = {0};
    if (!ws_find_header(resp, "Sec-WebSocket-Accept", accept, sizeof(accept))) {
        return false;
    }
    return ws_verify_accept(key, accept);
}

void feishu_ws_conn_init(feishu_ws_conn_t *conn)
{
    if (!conn) return;
    conn->sock = -1;
    conn->tls = false;
    conn->ctx = NULL;
    conn->ssl = NULL;
}

void feishu_ws_conn_close(feishu_ws_conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->ctx) {
        SSL_CTX_free(conn->ctx);
        conn->ctx = NULL;
    }
    if (conn->sock >= 0) {
        close(conn->sock);
        conn->sock = -1;
    }
    conn->tls = false;
}

int feishu_ws_wait_readable(feishu_ws_conn_t *conn, int timeout_ms)
{
    if (!conn || conn->sock < 0) return -1;
    if (conn->tls && conn->ssl && SSL_pending(conn->ssl) > 0) return 1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(conn->sock, &rfds);

    struct timeval tv;
    struct timeval *ptv = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }

    int ret = select(conn->sock + 1, &rfds, NULL, NULL, ptv);
    if (ret < 0) return -1;
    if (ret == 0) return 0;
    return FD_ISSET(conn->sock, &rfds) ? 1 : 0;
}

bool feishu_ws_connect(const char *url, feishu_ws_conn_t *conn)
{
    ws_url_t u;
    if (!ws_parse_url(url, &u) || !conn) return false;

    int sock = tcp_connect_timeout(u.host, u.port, FEISHU_WS_CONNECT_TIMEOUT);
    if (sock < 0) return false;

    conn->sock = sock;
    conn->tls = u.tls;
    conn->ctx = NULL;
    conn->ssl = NULL;

    if (u.tls) {
        if (!ws_tls_handshake(conn, u.host)) {
            feishu_ws_conn_close(conn);
            return false;
        }
    }

    if (!ws_handshake(conn, &u)) {
        feishu_ws_conn_close(conn);
        return false;
    }
    return true;
}

bool feishu_ws_send_frame_raw(feishu_ws_conn_t *conn, int opcode, const uint8_t *payload, size_t payload_len)
{
    if (!conn || conn->sock < 0) return false;
    if (payload_len > FEISHU_WS_MAX_PAYLOAD) return false;

    uint8_t header[14];
    size_t hlen = 0;
    header[0] = 0x80 | (opcode & 0x0F);

    if (payload_len <= 125) {
        header[1] = 0x80 | (uint8_t)payload_len;
        hlen = 2;
    } else if (payload_len <= 0xFFFF) {
        header[1] = 0x80 | 126;
        header[2] = (uint8_t)((payload_len >> 8) & 0xFF);
        header[3] = (uint8_t)(payload_len & 0xFF);
        hlen = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)((payload_len >> (56 - i * 8)) & 0xFF);
        }
        hlen = 10;
    }

    uint8_t mask[4];
    uint32_t r = (uint32_t)rand();
    mask[0] = (uint8_t)(r & 0xFF);
    mask[1] = (uint8_t)((r >> 8) & 0xFF);
    mask[2] = (uint8_t)((r >> 16) & 0xFF);
    mask[3] = (uint8_t)((r >> 24) & 0xFF);

    memcpy(header + hlen, mask, sizeof(mask));
    hlen += sizeof(mask);

    uint8_t *masked = NULL;
    if (payload_len > 0) {
        masked = malloc(payload_len);
        if (!masked) return false;
        for (size_t i = 0; i < payload_len; i++) {
            masked[i] = payload[i] ^ mask[i % 4];
        }
    }

    bool ok = (ws_conn_write_all(conn, header, hlen) >= 0);
    if (ok && payload_len > 0) {
        ok = (ws_conn_write_all(conn, masked, payload_len) >= 0);
    }
    free(masked);
    return ok;
}

bool feishu_ws_read_frame(feishu_ws_conn_t *conn, int *out_opcode, uint8_t **out_payload, size_t *out_len)
{
    uint8_t hdr[2];
    if (ws_read_exact(conn, hdr, sizeof(hdr), FEISHU_WS_READ_TIMEOUT) < 0) return false;

    int opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = (hdr[1] & 0x7F);

    if (len == 126) {
        uint8_t ext[2];
        if (ws_read_exact(conn, ext, sizeof(ext), FEISHU_WS_READ_TIMEOUT) < 0) return false;
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (ws_read_exact(conn, ext, sizeof(ext), FEISHU_WS_READ_TIMEOUT) < 0) return false;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | ext[i];
        }
    }

    if (len > FEISHU_WS_MAX_PAYLOAD) return false;

    uint8_t mask[4] = {0};
    if (masked) {
        if (ws_read_exact(conn, mask, sizeof(mask), FEISHU_WS_READ_TIMEOUT) < 0) return false;
    }

    uint8_t *payload = NULL;
    if (len > 0) {
        payload = malloc((size_t)len);
        if (!payload) return false;
        if (ws_read_exact(conn, payload, (size_t)len, FEISHU_WS_READ_TIMEOUT) < 0) {
            free(payload);
            return false;
        }
        if (masked) {
            for (uint64_t i = 0; i < len; i++) {
                payload[i] ^= mask[i % 4];
            }
        }
    }

    if (out_opcode) *out_opcode = opcode;
    if (out_payload) *out_payload = payload;
    if (out_len) *out_len = (size_t)len;
    return true;
}
