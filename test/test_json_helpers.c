#include <assert.h>
#include <string.h>
#include "json_helpers.h"
#include "cjson.h"

int main(void)
{
    /* Test json_string */
    cJSON *obj = cJSON_Parse("{\"name\":\"test\",\"count\":42}");
    assert(strcmp(json_string(obj, "name"), "test") == 0);
    assert(json_string(obj, "missing") == NULL);
    assert(json_string(obj, "count") == NULL); /* Not a string */
    
    /* Test json_number */
    assert(json_number(obj, "count") == 42);
    assert(json_number(obj, "missing") == -1);
    assert(json_number(obj, "name") == -1); /* Not a number */
    
    /* Test json_string_or_default */
    assert(strcmp(json_string_or_default(obj, "name", "fallback"), "test") == 0);
    assert(strcmp(json_string_or_default(obj, "missing", "fallback"), "fallback") == 0);
    
    cJSON_Delete(obj);
    return 0;
}
