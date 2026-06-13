/* 日志接口封装。 */

#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DAIMA_LOG_ERROR = 0,
    DAIMA_LOG_WARN  = 1,
    DAIMA_LOG_INFO  = 2,
    DAIMA_LOG_DEBUG = 3,
};

void daima_log_level_set(const char *tag, int level);
void daima_log_write(int level, const char *tag, const char *fmt, ...);

#define DAIMA_LOG_HOOK_PRE  0
#define DAIMA_LOG_HOOK_POST 1
typedef void (*daima_log_hook_t)(int phase, void *ctx);
void daima_log_set_hook(daima_log_hook_t hook);

#define DAIMA_LOGE(tag, fmt, ...) daima_log_write(DAIMA_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define DAIMA_LOGW(tag, fmt, ...) daima_log_write(DAIMA_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define DAIMA_LOGI(tag, fmt, ...) daima_log_write(DAIMA_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define DAIMA_LOGD(tag, fmt, ...) daima_log_write(DAIMA_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
