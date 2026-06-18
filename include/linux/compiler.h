/* 编译器提示宏：分支预测优化与返回值检查。 */

#pragma once

/* 提示编译器该分支很可能会执行（优化指令缓存布局） */
#define likely(x)   __builtin_expect(!!(x), 1)
/* 提示编译器该分支不太可能执行（优化指令缓存布局） */
#define unlikely(x) __builtin_expect(!!(x), 0)
/* 标记函数返回值不可忽略（否则产生编译警告） */
#define __must_check __attribute__((warn_unused_result))
