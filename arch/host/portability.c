/* 平台可移植层实现。
 * 所有 #ifdef 平台差异集中于此文件。
 */
#include "arch/host/portability.h"

#include <string.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

/* ---- memrchr ----------------------------------------------------------- */

void *host_memrchr(const void *s, int c, size_t n)
{
#ifdef __linux__
    return memrchr(s, c, n);
#else
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) {
        p--;
        if (*p == (unsigned char)c)
            return (void *)p;
    }
    return NULL;
#endif
}

/* ---- 空闲内存 ---------------------------------------------------------- */

size_t host_platform_free_memory(void)
{
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0)
        return (size_t)info.freeram;
#endif
    return 0;
}
