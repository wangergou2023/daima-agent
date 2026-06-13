#pragma once

#ifdef __GNUC__
#include_next <linux/kernel.h>
#endif

#include "err.h"
#include "linux/printk.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define IS_ENABLED(config) ((config) == 1)
#define BUG_ON(cond) do { if (cond) { pr_err("BUG: %s:%d\n", __FILE__, __LINE__); return -EINVAL; } } while (0)
#define WARN_ON(cond) do { if (cond) { pr_warn("WARN: %s:%d\n", __FILE__, __LINE__); } } while (0)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define round_up(x, y) (((x) + (y) - 1) / (y) * (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

static inline size_t strscpy(char *dst, const char *src, size_t size)
{
    if (!size)
        return 0;
    size_t len = strnlen(src, size - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return len;
}
