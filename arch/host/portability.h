/* arch 层可移植性接口。
 * 所有平台差异收敛于此，业务代码不再出现 #ifdef __linux__ / #ifdef __APPLE__。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* memrchr：GNU 扩展，macOS 缺失 */
void *host_memrchr(const void *s, int c, size_t n);

/* 当前可执行文件所在目录。 */
bool host_get_executable_dir(char *out, size_t out_size);

/* 平台空闲内存 */
size_t host_platform_free_memory(void);

#ifdef __cplusplus
}
#endif
