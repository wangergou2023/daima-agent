/* cJSON 辅助包装接口。 */

#pragma once

#include "cjson.h"

/** 获取 JSON 字符串字段，不存在返回 NULL。 */
const char *json_string(cJSON *obj, const char *key);

/** 获取 JSON 数字字段，不存在返回 -1。 */
int json_number(cJSON *obj, const char *key);

/** 获取 JSON 字符串字段，不存在返回 fallback。 */
const char *json_string_or_default(const cJSON *obj, const char *key, const char *fallback);
