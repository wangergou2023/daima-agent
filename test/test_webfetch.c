#include "paths.h"
#include "cJSON.h"
#include "drivers/tool/tool_webfetch.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0777); *p = '/'; }
    }
    mkdir(tmp, 0777);
}

static int run_tool(const char *input, char *output, size_t output_size)
{
    memset(output, 0, output_size);
    return tool_webfetch_execute(input, output, output_size);
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/daima-webfetch-test-%ld", (long)getpid());
    setenv("DAIMA_HOME", home, 1);
    char memory_dir[1024];
    snprintf(memory_dir, sizeof(memory_dir), "%s/spiffs_data/memory", home);
    mkdir_p(memory_dir);
    paths_init();

    char out[32768];
    int passed = 0, failed = 0;
    int ret;

#define TEST(name, input, expect) do { \
    printf("  %s: ", name); \
    ret = run_tool(input, out, sizeof(out)); \
    if (strstr(out, expect)) { printf("PASS\n"); passed++; } \
    else { printf("FAIL (expected '%s', got '%.200s')\n", expect, out); failed++; } \
} while(0)

#define TEST_ERR(name, input) do { \
    printf("  %s: ", name); \
    ret = run_tool(input, out, sizeof(out)); \
    if (ret != 0) { printf("PASS (error as expected)\n"); passed++; } \
    else { printf("FAIL (expected error, got OK)\n"); failed++; } \
} while(0)

    printf("=== SSRF Protection ===\n");
    TEST("block localhost", "{\"url\":\"http://localhost:8080/admin\"}", "URL 不允许");
    TEST("block 127.0.0.1", "{\"url\":\"http://127.0.0.1/test\"}", "URL 不允许");
    TEST("block 10.x", "{\"url\":\"http://10.0.0.1/api\"}", "URL 不允许");
    TEST("block 192.168.x", "{\"url\":\"http://192.168.1.1/\"}", "URL 不允许");
    TEST("block 172.16.x", "{\"url\":\"http://172.16.0.1/\"}", "URL 不允许");
    TEST("block 169.254.x", "{\"url\":\"http://169.254.1.1/\"}", "URL 不允许");
    TEST("block 0.0.0.0", "{\"url\":\"http://0.0.0.0/\"}", "URL 不允许");
    TEST("block non-http", "{\"url\":\"file:///etc/passwd\"}", "URL 不允许");
    TEST("block ftp", "{\"url\":\"ftp://evil.com\"}", "URL 不允许");

    printf("\n=== Input Validation ===\n");
    TEST_ERR("missing url", "{}");
    TEST_ERR("bad format", "{\"url\":\"https://example.com\",\"format\":\"xml\"}");
    TEST_ERR("bad json", "{bad");

    printf("\n=== Real HTTP Fetch ===\n");
    {
        printf("  fetch https://example.com: ");
        memset(out, 0, sizeof(out));
        ret = tool_webfetch_execute("{\"url\":\"https://example.com\"}", out, sizeof(out));
        if (strstr(out, "URL 不允许")) {
            printf("FAIL (SSRF wrongly blocked)\n"); failed++;
        } else if (strstr(out, "Example Domain") || strstr(out, "example")) {
            printf("PASS (fetched content)\n"); passed++;
        } else if (strstr(out, "HTTP 5") || strstr(out, "HTTP 429")) {
            printf("SKIP (upstream unavailable: %.80s)\n", out);
        } else {
            printf("FAIL (unexpected: %.200s)\n", out); failed++;
        }
    }

    {
        printf("  fetch https://httpbin.org/ip (text): ");
        memset(out, 0, sizeof(out));
        ret = tool_webfetch_execute("{\"url\":\"https://httpbin.org/ip\",\"format\":\"text\"}", out, sizeof(out));
        if (strstr(out, "origin")) {
            printf("PASS\n"); passed++;
        } else if (strstr(out, "URL 不允许")) {
            printf("FAIL (SSRF wrongly blocked)\n"); failed++;
        } else if (strstr(out, "HTTP 5") || strstr(out, "HTTP 429")) {
            printf("SKIP (upstream unavailable: %.80s)\n", out);
        } else {
            printf("FAIL (unexpected: %.200s)\n", out); failed++;
        }
    }

    {
        printf("  clean_url markdown link: ");
        memset(out, 0, sizeof(out));
        ret = tool_webfetch_execute("{\"url\":\"[ignored](https://example.com)\"}", out, sizeof(out));
        if (strstr(out, "Example Domain") || strstr(out, "example")) {
            printf("PASS (extracted URL from markdown)\n"); passed++;
        } else if (strstr(out, "URL 不允许")) {
            printf("FAIL (markdown URL rejected)\n"); failed++;
        } else {
            printf("FAIL (unexpected: %.200s)\n", out); failed++;
        }
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
