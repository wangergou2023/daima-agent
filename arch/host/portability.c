/* 平台可移植层实现。
 * 所有 #ifdef 平台差异集中于此文件。
 */
#include "arch/host/portability.h"
#include <string.h>
#include <stdio.h>

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
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    size_t free_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemAvailable: %zu kB", &free_kb) == 1) {
            fclose(f);
            return free_kb * 1024;
        }
    }
    fclose(f);
    return 0;
}
