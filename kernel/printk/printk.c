/* 内核风格日志实现：支持 KERN_<LEVEL> 前缀解析、日志等级过滤、HOOK 拦截。
 * 输出到 stderr + 日志文件，格式：HH:MM:SS.msc [X] tag: message。 */

#include "kernel/printk/printk.h"

#include "log_file.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int s_log_level = LOG_INFO;
static log_hook_t s_log_hook = NULL;

void log_level_set(const char *tag, int level)
{
    (void)tag;
    s_log_level = level;
}

void log_set_hook(log_hook_t hook)
{
    s_log_hook = hook;
}

static const char *level_char(int level)
{
    switch (level) {
    case LOG_ERROR: return "E";
    case LOG_WARN: return "W";
    case LOG_INFO: return "I";
    case LOG_DEBUG: return "D";
    default: return "?";
    }
}

/** 将内核日志等级（0-7）映射为 agent 内部的日志等级。 */
static int kernel_level_to_agent(int level)
{
    if (level <= 3) return LOG_ERROR;
    if (level == 4) return LOG_WARN;
    if (level == 7) return LOG_DEBUG;
    return LOG_INFO;
}

/** 从格式字符串中解析 <N> 前缀获取内核日志等级。若无法解析则返回 LOG_INFO。 */
static int printk_level_from_prefix(const char **fmt)
{
    const char *p = *fmt;
    if (p && p[0] == '<' && p[1] >= '0' && p[1] <= '7' && p[2] == '>') {
        *fmt = p + 3;
        return kernel_level_to_agent(p[1] - '0');
    }
    return LOG_INFO;
}

/** 核心日志写入：等级过滤 → hook 前处理 → 时间戳 → stderr → 日志文件 → hook 后处理。 */
static void log_vwrite(int level, const char *tag, const char *fmt, va_list ap)
{
    if (level > s_log_level) return;

    bool hook_state = false;
    if (s_log_hook) s_log_hook(LOG_HOOK_PRE, &hook_state);

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

    log_file_write(level, tag, msg_buf);

    if (s_log_hook) s_log_hook(LOG_HOOK_POST, &hook_state);
}

void log_write(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vwrite(level, tag, fmt, ap);
    va_end(ap);
}

/** 兼容 Linux 内核的 printk()——解析 <N> 前缀 → log_vwrite()。 */
int printk(const char *fmt, ...)
{
    int level = printk_level_from_prefix(&fmt);
    va_list ap;
    va_start(ap, fmt);
    log_vwrite(level, "kernel", fmt, ap);
    va_end(ap);
    return 0;
}
