#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERN_EMERG   "<0>"
#define KERN_ALERT   "<1>"
#define KERN_CRIT    "<2>"
#define KERN_ERR     "<3>"
#define KERN_WARNING "<4>"
#define KERN_NOTICE  "<5>"
#define KERN_INFO    "<6>"
#define KERN_DEBUG   "<7>"

enum {
    DAIMA_LOG_ERROR = 0,
    DAIMA_LOG_WARN  = 1,
    DAIMA_LOG_INFO  = 2,
    DAIMA_LOG_DEBUG = 3,
};

int printk(const char *fmt, ...);
void daima_log_level_set(const char *tag, int level);
void daima_log_write(int level, const char *tag, const char *fmt, ...);

#define DAIMA_LOG_HOOK_PRE  0
#define DAIMA_LOG_HOOK_POST 1
typedef void (*daima_log_hook_t)(int phase, void *ctx);
void daima_log_set_hook(daima_log_hook_t hook);

#define pr_emerg(fmt, ...) printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)   printk(KERN_ERR fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)  printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)  printk(KERN_INFO fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) printk(KERN_DEBUG fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
