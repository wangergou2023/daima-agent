/* 可加载模块系统接口：模块声明宏与模块级 init/exit。 */

#pragma once
#include "linux/init.h"

/* 模块许可声明（当前为占位宏） */
#define MODULE_LICENSE(x)
/* 模块作者声明（当前为占位宏） */
#define MODULE_AUTHOR(x)
/* 模块描述声明（当前为占位宏） */
#define MODULE_DESCRIPTION(x)

/* 模块初始化函数：映射为 device_initcall（第 6 级） */
#define module_init(fn)  device_initcall(fn)
/* 模块退出函数（当前为占位宏） */
#define module_exit(fn)
