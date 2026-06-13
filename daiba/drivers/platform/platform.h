/* 平台抽象接口。 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t daima_get_free_memory(void);
size_t daima_get_largest_free_block(void);
void *daima_calloc(size_t n, size_t size);
void *daima_realloc(void *ptr, size_t size);
uint32_t daima_random(void);
int64_t daima_time_us(void);

#ifdef __cplusplus
}
#endif
