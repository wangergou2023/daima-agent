#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "drivers/channel/gateway/ws_client.h"

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int read_socket_text(int fd, char *buf, size_t buf_size)
{
    ssize_t n;

    if (!buf || buf_size == 0) {
        return -1;
    }
    n = recv(fd, buf, buf_size - 1, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            buf[0] = '\0';
            return 0;
        }
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}

int main(void)
{
    int fds[2];
    char out[2048];
    const char *chat_id = "probe_chat_pending";
    const char *reply = "pending parent reply";
    const char *payload = "{\"type\":\"message\",\"chat_id\":\"probe_chat_pending\",\"content\":\"hi\"}";
    const char *sync_payload = "{\"type\":\"session_sync\",\"chat_id\":\"probe_chat_pending\"}";
    void *client = NULL;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        return 1;
    }
    if (set_nonblock(fds[1]) != 0) {
        perror("fcntl");
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    ws_client_session_init();
    ws_pending_save(chat_id, reply);
    if (!ws_client_session_add(fds[0])) {
        close(fds[0]);
        close(fds[1]);
        return fail("unable to add websocket session");
    }

    client = ws_client_find_session_for_test(fds[0]);
    if (!client) {
        close(fds[0]);
        close(fds[1]);
        return fail("unable to locate websocket session");
    }

    if (read_socket_text(fds[1], out, sizeof(out)) < 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("unexpected socket read error before chat_id resolution");
    }
    if (out[0] != '\0') {
        close(fds[0]);
        close(fds[1]);
        return fail("pending replay happened before real chat_id resolution");
    }

    ws_client_dispatch_text_frame_for_test(fds[0], client, payload, time(NULL));

    if (read_socket_text(fds[1], out, sizeof(out)) <= 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("missing pending replay after chat_id resolution");
    }
    if (!strstr(out, "\"type\":\"response\"")) {
        close(fds[0]);
        close(fds[1]);
        return fail("pending replay did not emit response envelope");
    }
    if (!strstr(out, "\"chat_id\":\"probe_chat_pending\"")) {
        close(fds[0]);
        close(fds[1]);
        return fail("pending replay used wrong chat_id");
    }
    if (!strstr(out, "pending parent reply")) {
        close(fds[0]);
        close(fds[1]);
        return fail("pending replay lost response content");
    }

    if (read_socket_text(fds[1], out, sizeof(out)) < 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("unexpected socket read error after replay");
    }
    if (out[0] != '\0') {
        close(fds[0]);
        close(fds[1]);
        return fail("pending replay was not cleared after delivery");
    }

    ws_pending_save(chat_id, reply);
    ws_client_dispatch_text_frame_for_test(fds[0], client, sync_payload, time(NULL));

    if (read_socket_text(fds[1], out, sizeof(out)) <= 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("missing replay after session_sync");
    }
    if (!strstr(out, "\"type\":\"response\"")) {
        close(fds[0]);
        close(fds[1]);
        return fail("session_sync replay did not emit response envelope");
    }
    if (!strstr(out, "pending parent reply")) {
        close(fds[0]);
        close(fds[1]);
        return fail("session_sync replay lost response content");
    }

    close(fds[0]);
    close(fds[1]);
    puts("PASS");
    return 0;
}
