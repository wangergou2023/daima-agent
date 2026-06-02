/* Vector 动画 + 屏幕显示工具 */
#include "tools/tool_vector_common.h"
#include "tools/tool_registry.h"
#include "channels/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "daima_log.h"

static mcp_client_t *get_mcp(char *output, size_t size) {
    mcp_client_t *m = vector_channel_get_mcp();
    if (!m) snprintf(output, size, "错误：Vector 机器人未连接");
    return m;
}

static daima_err_t call_mcp_with_args(mcp_client_t *mcp, const char *tool_name, cJSON *args,
                                      char *output, size_t output_size)
{
    char *args_json = cJSON_PrintUnformatted(args);
    if (!args_json) {
        cJSON_Delete(args);
        snprintf(output, output_size, "错误：JSON 序列化失败");
        return DAIMA_ERR_NO_MEM;
    }
    daima_err_t err = mcp_client_call_tool(mcp, tool_name, args_json, output, output_size);
    free(args_json);
    cJSON_Delete(args);
    return err;
}

/* ---- Play Animation ---- */
static daima_err_t tool_robot_play_animation_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;

    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }

    cJSON *n = cJSON_GetObjectItem(in, "name");
    cJSON *l = cJSON_GetObjectItem(in, "loops");
    const char *name = n && cJSON_IsString(n) ? n->valuestring : "";
    if (!name[0]) { snprintf(output, output_size, "错误：缺少动画名称"); cJSON_Delete(in); return DAIMA_ERR_INVALID_ARG; }
    int loops = l && cJSON_IsNumber(l) ? (int)l->valuedouble : 1;

    cJSON *args = cJSON_CreateObject();
    if (!args) {
        cJSON_Delete(in);
        return DAIMA_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(args, "name", name);
    cJSON_AddNumberToObject(args, "loops", loops);
    cJSON_Delete(in);
    return call_mcp_with_args(mcp, "robot_play_animation", args, output, output_size);
}

static const daima_tool_t s_play_animation = {
    .name = "robot_play_animation",
    .description = "在 Vector 脸部屏幕上播放动画效果。常用: WeatherStars01(星星/烟花), WeatherRain01, WeatherSnow01, WeatherSunny01, WeatherThunderstorm01, Greeting01, Happy01, Sad01, Surprise01, Sleep01, WakeUp01, Celebrating01, Love01, LevelUp01。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"name\":{\"type\":\"string\",\"description\":\"动画名称，如 WeatherStars01\"},"
        "\"loops\":{\"type\":\"integer\",\"description\":\"循环次数，默认1\"}"
        "},\"required\":[\"name\"]}",
    .execute = tool_robot_play_animation_execute,
};

const daima_tool_t *tool_robot_play_animation_definition(void) { return &s_play_animation; }
