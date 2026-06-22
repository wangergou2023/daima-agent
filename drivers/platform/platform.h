/* 平台抽象接口。 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t platform_free_memory(void);
size_t platform_largest_free_block(void);
bool platform_format_bytes(size_t bytes, char *buf, size_t buf_size);
void *platform_calloc(size_t n, size_t size);
void *platform_realloc(void *ptr, size_t size);
uint32_t platform_random(void);
int64_t platform_time_us(void);

#ifdef __cplusplus
}
#endif
