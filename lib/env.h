/* 环境变量辅助接口：获取、类型转换（int/bool）、设置。 */

#pragma once

#include <stdbool.h>

/** 获取环境变量值，空字符串视为不存在。 */
const char *env_get(const char *name);

/** 获取 int 类型环境变量，解析失败返回默认值。 */
int env_int_or_default(const char *name, int fallback);

/** 获取 bool 类型环境变量（1/true/yes → true）。 */
bool env_bool_or_default(const char *name, bool fallback);

/** 设置环境变量（覆盖已有值）。 */
void env_set(const char *name, const char *value);
