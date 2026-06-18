/* 内核内存分配器封装：kmalloc/kzalloc/kfree，统一分配接口。 */

#pragma once

#include <stdlib.h>

/* GFP_KERNEL 标志：内核上下文正常分配（当前实现忽略 flags） */
#define GFP_KERNEL 0

/**
 * 分配指定大小的内核内存。
 * @param size   分配字节数
 * @param flags  分配标志（GFP_KERNEL 等，当前实现忽略）
 * @return 成功返回内存指针，失败返回 NULL
 */
static inline void *kmalloc(size_t size, int flags)
{
	(void)flags;
	return malloc(size);
}

/**
 * 分配并清零指定大小的内核内存。
 * @param size   分配字节数
 * @param flags  分配标志（GFP_KERNEL 等，当前实现忽略）
 * @return 成功返回已清零的内存指针，失败返回 NULL
 */
static inline void *kzalloc(size_t size, int flags)
{
	(void)flags;
	return calloc(1, size);
}

/**
 * 释放内核内存。
 * @param p  要释放的内存指针（NULL 安全）
 */
static inline void kfree(void *p)
{
	free(p);
}
