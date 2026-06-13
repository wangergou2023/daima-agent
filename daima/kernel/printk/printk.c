#include "kernel/printk/printk.h"

#include "log_file.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int s_log_level = DAIMA_LOG_INFO;
static daima_log_hook_t s_log_hook = NULL;

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

static int kernel_level_to_daima(int level)
{
    if (level <= 3) return DAIMA_LOG_ERROR;
    if (level == 4) return DAIMA_LOG_WARN;
    if (level == 7) return DAIMA_LOG_DEBUG;
    return DAIMA_LOG_INFO;
}

static int printk_level_from_prefix(const char **fmt)
{
    const char *p = *fmt;
    if (p && p[0] == '<' && p[1] >= '0' && p[1] <= '7' && p[2] == '>') {
        *fmt = p + 3;
        return kernel_level_to_daima(p[1] - '0');
    }
    return DAIMA_LOG_INFO;
}

static void daima_log_vwrite(int level, const char *tag, const char *fmt, va_list ap)
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

    char msg_buf[1024];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    fprintf(stderr, "%s\n", msg_buf);
    fflush(stderr);

    daima_log_file_write(level, tag, msg_buf);

    if (s_log_hook) s_log_hook(DAIMA_LOG_HOOK_POST, &hook_state);
}

void daima_log_write(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    daima_log_vwrite(level, tag, fmt, ap);
    va_end(ap);
}

int printk(const char *fmt, ...)
{
    int level = printk_level_from_prefix(&fmt);
    va_list ap;
    va_start(ap, fmt);
    daima_log_vwrite(level, "kernel", fmt, ap);
    va_end(ap);
    return 0;
}
