#include "err.h"
#include "log.h"
#include "drivers/platform/platform.h"
#include "log_file.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#endif

static int s_log_level = DAIMA_LOG_INFO;
static daima_log_hook_t s_log_hook = NULL;
static int s_rand_seeded = 0;

const char *daima_err_to_name(daima_err_t err)
{
    switch (err) {
    case DAIMA_OK: return "DAIMA_OK";
    case DAIMA_FAIL: return "DAIMA_FAIL";
    case DAIMA_ERR_NO_MEM: return "DAIMA_ERR_NO_MEM";
    case DAIMA_ERR_INVALID_ARG: return "DAIMA_ERR_INVALID_ARG";
    case DAIMA_ERR_INVALID_STATE: return "DAIMA_ERR_INVALID_STATE";
    case DAIMA_ERR_INVALID_SIZE: return "DAIMA_ERR_INVALID_SIZE";
    case DAIMA_ERR_TIMEOUT: return "DAIMA_ERR_TIMEOUT";
    case DAIMA_ERR_NOT_FOUND: return "DAIMA_ERR_NOT_FOUND";
    case DAIMA_ERR_HTTP_CONNECT: return "DAIMA_ERR_HTTP_CONNECT";
    case DAIMA_ERR_HTTP_WRITE_DATA: return "DAIMA_ERR_HTTP_WRITE_DATA";
    case DAIMA_ERR_HTTP_FETCH_HEADER: return "DAIMA_ERR_HTTP_FETCH_HEADER";
    case DAIMA_ERR_NVS_NOT_FOUND: return "DAIMA_ERR_NVS_NOT_FOUND";
    case DAIMA_ERR_NVS_NO_FREE_PAGES: return "DAIMA_ERR_NVS_NO_FREE_PAGES";
    case DAIMA_ERR_NVS_NEW_VERSION_FOUND: return "DAIMA_ERR_NVS_NEW_VERSION_FOUND";
    default: return "DAIMA_ERR_UNKNOWN";
    }
}

void daima_log_level_set(const char *tag, int level)
{
    (void)tag;
    s_log_level = level;
}

void daima_log_set_hook(daima_log_hook_t hook)
{
    s_log_hook = hook;
}

static const char *level_char(int level)
{
    switch (level) {
    case DAIMA_LOG_ERROR: return "E";
    case DAIMA_LOG_WARN: return "W";
    case DAIMA_LOG_INFO: return "I";
    case DAIMA_LOG_DEBUG: return "D";
    default: return "?";
    }
}

#include <sys/syscall.h>

void daima_log_write(int level, const char *tag, const char *fmt, ...)
{
    if (level > s_log_level) return;

    bool hook_state = false;
    if (s_log_hook) s_log_hook(DAIMA_LOG_HOOK_PRE, &hook_state);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    fprintf(stderr, "%s.%03ld [%s] %s: ", ts, tv.tv_usec / 1000,
            level_char(level), tag ? tag : "log");

    va_list ap;
    va_start(ap, fmt);

    char msg_buf[1024];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    fprintf(stderr, "%s\n", msg_buf);
    fflush(stderr);

    daima_log_file_write(level, tag, msg_buf);

    va_end(ap);

    if (s_log_hook) s_log_hook(DAIMA_LOG_HOOK_POST, &hook_state);
}

size_t daima_get_free_memory(void)
{
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (size_t)info.freeram;
    }
#endif
    return 0;
}

size_t daima_get_largest_free_block(void)
{
    return daima_get_free_memory();
}

void *daima_calloc(size_t n, size_t size)
{
    return calloc(n, size);
}

void *daima_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

uint32_t daima_random(void)
{
    if (!s_rand_seeded) {
        s_rand_seeded = 1;
        srand((unsigned)time(NULL));
    }
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

int64_t daima_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000LL) + (int64_t)tv.tv_usec;
}
