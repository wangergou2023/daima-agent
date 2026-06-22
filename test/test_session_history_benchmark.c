#include "paths.h"
#include "runtime.h"
#include "drivers/memory/session_store.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int printk(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static long elapsed_us(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000000L +
           (end.tv_nsec - start.tv_nsec) / 1000L;
}

static void write_history(const char *chat_id, int count)
{
    char content[160];
    for (int i = 0; i < count; ++i) {
        snprintf(content, sizeof(content),
                 "message-%04d: benchmark payload for history loading", i);
        assert(session_store_append(chat_id, (i % 2 == 0) ? "user" : "assistant", content) == 0);
    }
}

static void benchmark_case(const char *chat_id, int count)
{
    char history[131072];
    struct timespec start;
    struct timespec end;

    write_history(chat_id, count);

    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    assert(session_store_get_history_json(chat_id, history, sizeof(history), -1) == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);

    printf("history_count=%d bytes=%zu elapsed_us=%ld\n",
           count, strlen(history), elapsed_us(start, end));
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/agent-session-bench-home-%ld", (long)getpid());
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir_p(path_config_dir());
    mkdir_p(path_session_dir());

    assert(runtime_config_init() == 0);
    assert(session_store_init() == 0);

    benchmark_case("bench_16", 16);
    benchmark_case("bench_96", 96);
    benchmark_case("bench_512", 512);
    benchmark_case("bench_2048", 2048);

    return 0;
}
