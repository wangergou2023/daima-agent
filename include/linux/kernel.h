/* 内核基础宏与工具函数：编译期数组大小、内省、安全字符串拷贝。 */

#pragma once

#include "err.h"
#include "linux/printk.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

/* 编译期获取数组元素个数 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
/* 编译期检查配置项是否启用 */
#define IS_ENABLED(config) ((config) == 1)
/* 断言失败时打印错误并返回 -EINVAL */
#define BUG_ON(cond) do { if (cond) { pr_err("BUG: %s:%d\n", __FILE__, __LINE__); return -EINVAL; } } while (0)
/* 条件为真时打印警告 */
#define WARN_ON(cond) do { if (cond) { pr_warn("WARN: %s:%d\n", __FILE__, __LINE__); } } while (0)
/* 通过成员指针反推包含它的父结构体指针（内核内省核心宏） */
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
/* 向上对齐到 y 的倍数 */
#define round_up(x, y) (((x) + (y) - 1) / (y) * (y))
/* 取两值中较小者 */
#define min(x, y) ((x) < (y) ? (x) : (y))
/* 取两值中较大者 */
#define max(x, y) ((x) > (y) ? (x) : (y))

/**
 * 安全字符串拷贝（总是以 \0 结尾）。
 * @param dst   目标缓冲区
 * @param src   源字符串
 * @param size  目标缓冲区大小
 * @return 拷贝的字符数（不含终止符）
 */
static inline size_t strscpy(char *dst, const char *src, size_t size)
{
	if (!size)
		return 0;
	size_t len = strnlen(src, size - 1);
	memcpy(dst, src, len);
	dst[len] = '\0';
	return len;
}
