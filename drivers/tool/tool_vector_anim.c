/* Vector 动画 + 屏幕显示工具 */

#include "drivers/tool/tool_vector_common.h"

#include "drivers/tool/tool_types.h"

#include "drivers/channel/vector/vector_channel.h"



#include <stdio.h>

#include <stdlib.h>

#include <string.h>



#include "cjson.h"

#include "linux/printk.h"

static err_t tool_robot_play_animation_execute(const char *input_json, char *output, size_t output_size)

{

    mcp_client_t *mcp = tool_get_mcp(output, output_size);

    if (!mcp) return ERR_FAIL;



    cJSON *in = cJSON_Parse(input_json);

    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return ERR_INVALID_ARG; }



    cJSON *n = cJSON_GetObjectItem(in, "name");

    cJSON *l = cJSON_GetObjectItem(in, "loops");

    const char *name = n && cJSON_IsString(n) ? n->valuestring : "";

    if (!name[0]) { snprintf(output, output_size, "错误：缺少动画名称"); cJSON_Delete(in); return ERR_INVALID_ARG; }

    int loops = l && cJSON_IsNumber(l) ? (int)l->valuedouble : 1;



    cJSON *args = cJSON_CreateObject();

    if (!args) {

        cJSON_Delete(in);

        return ERR_NO_MEM;

    }

    cJSON_AddStringToObject(args, "name", name);

    cJSON_AddNumberToObject(args, "loops", loops);

    cJSON_Delete(in);

    return call_mcp_with_args(mcp, "robot_play_animation", args, output, output_size);

}



static const struct tool s_play_animation = {

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



const struct tool *tool_robot_play_animation_definition(void) { return &s_play_animation; }


static int play_animation_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_play_animation_device = {
    .name = "robot_play_animation",
    .description = "在 Vector 脸部屏幕上播放动画效果。常用: WeatherStars01(星星/烟花), WeatherRain01, WeatherSnow01, WeatherSunny01, WeatherThunderstorm01, Greeting01, Happy01, Sad01, Surprise01, Sleep01, WakeUp01, Celebrating01, Love01, LevelUp01。",
    .input_schema_json = "{\"type\":\"object\"," "\"properties\":{" "\"name\":{\"type\":\"string\",\"description\":\"动画名称，如 WeatherStars01\"}," "\"loops\":{\"type\":\"integer\",\"description\":\"循环次数，默认1\"}" "},\"required\":[\"name\"]}",
};

static struct tool_driver s_play_animation_driver = {
    .drv.name = "robot_play_animation",
    .drv.probe = play_animation_tool_probe,
    .execute = tool_robot_play_animation_execute,
};

const struct tool_device *tool_robot_play_animation_device(void)
{
    return &s_play_animation_device;
}

const struct tool_driver *tool_robot_play_animation_driver(void)
{
    return &s_play_animation_driver;
}
