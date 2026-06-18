/* 内核基本类型定义：标准长度的有符号/无符号整数。 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 有符号/无符号固定宽度整数（内核命名约定 __sN/__uN） */
typedef int8_t __s8;                /* 有符号 8 位 */
typedef uint8_t __u8;              /* 无符号 8 位 */
typedef int16_t __s16;             /* 有符号 16 位 */
typedef uint16_t __u16;            /* 无符号 16 位 */
typedef int32_t __s32;             /* 有符号 32 位 */
typedef uint32_t __u32;            /* 无符号 32 位 */
typedef int64_t __s64;             /* 有符号 64 位 */
typedef uint64_t __u64;            /* 无符号 64 位 */
/* 平台相关的长整数类型 */
typedef long __kernel_long_t;      /* 有符号长整数 */
typedef unsigned long __kernel_ulong_t; /* 无符号长整数 */
