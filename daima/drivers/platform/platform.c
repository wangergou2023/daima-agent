#include "err.h"
#include "drivers/platform/platform.h"

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#include "linux/slab.h"
#endif

static int s_rand_seeded = 0;

const char *err_name(err_t err)
{
    switch (err) {
    case 0: return "DAIMA_OK";
    case ERR_FAIL: return "DAIMA_FAIL";
    case ERR_NO_MEM: return "DAIMA_ERR_NO_MEM";
    case ERR_INVALID_ARG: return "DAIMA_ERR_INVALID_ARG";
    case ERR_INVALID_STATE: return "DAIMA_ERR_INVALID_STATE";
    case ERR_INVALID_SIZE: return "DAIMA_ERR_INVALID_SIZE";
    case ERR_TIMEOUT: return "DAIMA_ERR_TIMEOUT";
    case ERR_NOT_FOUND: return "DAIMA_ERR_NOT_FOUND";
    case ERR_HTTP_CONNECT: return "DAIMA_ERR_HTTP_CONNECT";
    case ERR_HTTP_WRITE_DATA: return "DAIMA_ERR_HTTP_WRITE_DATA";
    case ERR_HTTP_FETCH_HEADER: return "DAIMA_ERR_HTTP_FETCH_HEADER";
    case ERR_NVS_NOT_FOUND: return "DAIMA_ERR_NVS_NOT_FOUND";
    case ERR_NVS_NO_FREE_PAGES: return "DAIMA_ERR_NVS_NO_FREE_PAGES";
    case ERR_NVS_NEW_VERSION_FOUND: return "DAIMA_ERR_NVS_NEW_VERSION_FOUND";
    default: return "DAIMA_ERR_UNKNOWN";
    }
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
    return kzalloc(n * size, GFP_KERNEL);
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
