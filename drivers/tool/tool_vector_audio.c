/* Vector 音频工具: set_volume */

#include "drivers/tool/tool_vector_common.h"

#include "drivers/tool/tool_registry.h"

#include "drivers/channel/vector/vector_channel.h"



#include <stdio.h>

#include <stdlib.h>

#include <string.h>



#include "cjson.h"

#include "linux/printk.h"

static err_t tool_robot_set_volume_execute(const char *input_json, char *output, size_t output_size)

{

    mcp_client_t *mcp = tool_get_mcp(output, output_size);

    if (!mcp) return ERR_FAIL;



    cJSON *in = cJSON_Parse(input_json);

    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return ERR_INVALID_ARG; }

    cJSON *lev = cJSON_GetObjectItem(in, "level");

    int level = lev && cJSON_IsNumber(lev) ? (int)lev->valuedouble : 2;

    if (level < 0) level = 0;

    if (level > 4) level = 4;

    cJSON_Delete(in);



    cJSON *args = cJSON_CreateObject();

    if (!args) return ERR_NO_MEM;

    cJSON_AddNumberToObject(args, "level", level);

    return call_mcp_with_args(mcp, "robot_set_volume", args, output, output_size);

}



static const struct tool s_set_volume = {

    .name = "robot_set_volume",

    .description = "设置 Vector 机器人主音量。level: 0(最低)~4(最高)。",

    .input_schema_json =

        "{\"type\":\"object\","

        "\"properties\":{\"level\":{\"type\":\"integer\",\"description\":\"音量0~4\"}},"

        "\"required\":[\"level\"]}",

    .execute = tool_robot_set_volume_execute,

};



const struct tool *tool_robot_set_volume_definition(void) { return &s_set_volume; }


static int set_volume_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_set_volume_device = {
    .name = "robot_set_volume",
    .description = "设置 Vector 机器人主音量。level: 0(最低)~4(最高)。",
    .input_schema_json = "{\"type\":\"object\"," "\"properties\":{\"level\":{\"type\":\"integer\",\"description\":\"音量0~4\"}}," "\"required\":[\"level\"]}",
};

static struct tool_driver s_set_volume_driver = {
    .name = "robot_set_volume",
    .probe = set_volume_tool_probe,
    .execute = tool_robot_set_volume_execute,
};

const struct tool_device *tool_set_volume_device(void)
{
    return &s_set_volume_device;
}

const struct tool_driver *tool_set_volume_driver(void)
{
    return &s_set_volume_driver;
}
