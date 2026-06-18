/* 可加载模块系统接口 — 简化版（移除无用的空宏和 device_initcall 映射） */

#pragma once
#include "linux/init.h"

/* 模块初始化函数（保留以兼容 extensions/ 现有代码） */
#define module_init(fn)  device_initcall(fn)
