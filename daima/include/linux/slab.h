#pragma once

#include <stdlib.h>

static inline void *kmalloc(size_t size, int flags)
{
    (void)flags;
    return malloc(size);
}

static inline void *kzalloc(size_t size, int flags)
{
    (void)flags;
    return calloc(1, size);
}

static inline void kfree(void *p)
{
    free(p);
}

#define GFP_KERNEL 0
