#pragma once

#ifdef __GNUC__
#include_next <linux/kernel.h>
#endif

#include <stddef.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define IS_ENABLED(config) ((config) == 1)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define round_up(x, y) (((x) + (y) - 1) / (y) * (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
