#include "err.h"
#include "drivers/platform/platform.h"
#include "arch/host/portability.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "linux/slab.h"

static int s_rand_seeded = 0;

const char *err_name(err_t err)
{
    switch (err) {
    case 0: return "0";
    case ERR_FAIL: return "ERR_FAIL";
    case ERR_NO_MEM: return "ERR_NO_MEM";
    case ERR_INVALID_ARG: return "ERR_INVALID_ARG";
    case ERR_INVALID_STATE: return "ERR_INVALID_STATE";
    case ERR_INVALID_SIZE: return "ERR_INVALID_SIZE";
    case ERR_TIMEOUT: return "ERR_TIMEOUT";
    case ERR_NOT_FOUND: return "ERR_NOT_FOUND";
    case ERR_HTTP_CONNECT: return "ERR_HTTP_CONNECT";
    case ERR_HTTP_WRITE_DATA: return "ERR_HTTP_WRITE_DATA";
    case ERR_HTTP_FETCH_HEADER: return "ERR_HTTP_FETCH_HEADER";
    case ERR_NVS_NOT_FOUND: return "ERR_NVS_NOT_FOUND";
    case ERR_NVS_NO_FREE_PAGES: return "ERR_NVS_NO_FREE_PAGES";
    case ERR_NVS_NEW_VERSION_FOUND: return "ERR_NVS_NEW_VERSION_FOUND";
    default: return "ERR_UNKNOWN";
    }
}

size_t platform_free_memory(void)
{
    return host_platform_free_memory();
}

size_t platform_largest_free_block(void)
{
    return platform_free_memory();
}

bool platform_format_bytes(size_t bytes, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return false;
    }

    int n = snprintf(buf, buf_size, "%zu", bytes);
    if (n < 0 || (size_t)n >= buf_size) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

void *platform_calloc(size_t n, size_t size)
{
    return kzalloc(n * size, GFP_KERNEL);
}

void *platform_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

uint32_t platform_random(void)
{
    if (!s_rand_seeded) {
        s_rand_seeded = 1;
        srand((unsigned)time(NULL));
    }
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

int64_t platform_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000LL) + (int64_t)tv.tv_usec;
}
