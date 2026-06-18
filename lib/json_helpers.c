/* cJSON 辅助包装：安全获取 string/int 字段，避免直接访问 cJSON 内部字段。 */

#include "json_helpers.h"

/**
 * 从 JSON 对象获取字符串字段。
 * @param obj JSON 对象
 * @param key 字段名
 * @return 字符串指针（不可修改），不存在返回 NULL
 */
const char *json_string(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : NULL;
}

/**
 * 从 JSON 对象获取数字字段。
 * @param obj JSON 对象
 * @param key 字段名
 * @return 整数值，不存在返回 -1
 */
int json_number(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : -1;
}

/**
 * 从 JSON 对象获取字符串字段，不存在时返回默认值。
 * @param obj      JSON 对象
 * @param key      字段名
 * @param fallback 默认值
 * @return 字符串指针（不可修改）
 */
const char *json_string_or_default(const cJSON *obj, const char *key, const char *fallback)
{
    cJSON *item = cJSON_GetObjectItem((cJSON *)obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : fallback;
}
