/* 运行时配置 JSON helper。 */

#include "runtime_internal.h"

#include "linux/kernel.h"

const cJSON *runtime_config_get_object_item(const cJSON *root, const char *key)
{
    if (!root || !cJSON_IsObject(root)) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive((cJSON *)root, key);
}

bool runtime_config_json_copy_string(const cJSON *root, const char *key, char *out, size_t out_size)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return false;
    }
    strscpy(out, item->valuestring, out_size);
    return true;
}

bool runtime_config_json_read_int(const cJSON *root, const char *key, int *out)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !cJSON_IsNumber(item) || !out) {
        return false;
    }
    *out = (int)item->valuedouble;
    return true;
}

bool runtime_config_json_read_bool(const cJSON *root, const char *key, bool *out)
{
    const cJSON *item = runtime_config_get_object_item(root, key);
    if (!item || !out) {
        return false;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble != 0;
        return true;
    }
    return false;
}
