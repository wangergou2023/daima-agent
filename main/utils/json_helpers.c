#include "utils/json_helpers.h"

const char *json_string(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : NULL;
}

int json_number(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : -1;
}

const char *json_string_or_default(const cJSON *obj, const char *key, const char *fallback)
{
    cJSON *item = cJSON_GetObjectItem((cJSON *)obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : fallback;
}
