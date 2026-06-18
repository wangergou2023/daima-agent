#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arch/host/llm_http_client_host.h"

static void test_async_request_returns_body_without_blocking_start(void)
{
    char template[] = "/tmp/agent-async-http-XXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    const char *expected = "hello async";
    assert(write(fd, expected, strlen(expected)) == (ssize_t)strlen(expected));
    close(fd);

    char url[256];
    snprintf(url, sizeof(url), "file://%s", template);

    llm_async_request_t *req = llm_http_async_request(
        "GET",
        url,
        NULL,
        NULL,
        3000);

    assert(req != NULL);

    for (int i = 0; i < 100 && !llm_http_async_is_done(req); i++) {
        usleep(1000);
    }

    char *body = NULL;
    long status = -1;
    int err = llm_http_async_get_response(req, &body, &status);

    assert(!err);
    assert(body != NULL);
    assert(strcmp(body, "hello async") == 0);
    assert(!status);

    free(body);
    llm_http_async_free(req);
    unlink(template);
}

int main(void)
{
    test_async_request_returns_body_without_blocking_start();
    printf("test_llm_http_async passed\n");
    return 0;
}
