/* 环境变量辅助：获取、类型转换（int/bool）、设置。 */

#include "env.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/**
 * 获取环境变量值，空字符串视为不存在。
 * @param name 环境变量名
 * @return 值指针（不可修改），不存在返回 NULL
 */
const char *env_get(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    const char *raw = getenv(name);
    if (raw && raw[0]) {
        return raw;
    }
    return NULL;
}

/**
 * 获取 int 类型环境变量，解析失败返回默认值。
 * @param name     环境变量名
 * @param fallback 默认值
 * @return 解析后的 int 值
 */
int env_int_or_default(const char *name, int fallback)
{
    const char *raw = env_get(name);
    if (!raw) {
        return fallback;
    }
    char *end = NULL;
    long val = strtol(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) {
        return fallback;
    }
    return (int)val;
}

/**
 * 获取 bool 类型环境变量。支持: 1/true/yes → true, 0/false/no → false。
 * @param name     环境变量名
 * @param fallback 默认值
 * @return 解析后的 bool 值
 */
bool env_bool_or_default(const char *name, bool fallback)
{
    const char *raw = env_get(name);
    if (!raw) {
        return fallback;
    }
    if (strcmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 || strcasecmp(raw, "yes") == 0) {
        return true;
    }
    if (strcmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 || strcasecmp(raw, "no") == 0) {
        return false;
    }
    return fallback;
}

/**
 * 设置环境变量（覆盖已有值）。
 * @param name  环境变量名
 * @param value 环境变量值
 */
void env_set(const char *name, const char *value)
{
    if (!name || !name[0] || !value || !value[0]) {
        return;
    }
    setenv(name, value, 1);
}
