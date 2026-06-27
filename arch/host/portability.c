/* 平台可移植层实现。
 * 所有 #ifdef 平台差异集中于此文件。
 */
#include "arch/host/portability.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif

#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
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

/* ---- executable dir ---------------------------------------------------- */

bool host_get_executable_dir(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }

    char exe_path[PATH_MAX];
    exe_path[0] = '\0';

#ifdef __APPLE__
    uint32_t size = (uint32_t)sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return false;
    }
#else
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0 || n >= (ssize_t)sizeof(exe_path)) {
        return false;
    }
    exe_path[n] = '\0';
#endif

    char resolved[PATH_MAX];
    const char *path = realpath(exe_path, resolved);
    if (!path) {
        path = exe_path;
    }

    char *slash = strrchr((char *)path, '/');
    if (!slash) {
        return false;
    }

    size_t len = (size_t)(slash - path);
    if (len == 0 || len >= out_size) {
        return false;
    }

    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

/* ---- 空闲内存 ---------------------------------------------------------- */

size_t host_platform_free_memory(void)
{
#ifdef __APPLE__
    uint64_t free_bytes = 0;
    size_t len = sizeof(free_bytes);
    if (sysctlbyname("hw.memsize", &free_bytes, &len, NULL, 0) == 0 &&
        free_bytes > 0) {
        return (size_t)free_bytes;
    }
    return 0;
#else
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
#endif
}
