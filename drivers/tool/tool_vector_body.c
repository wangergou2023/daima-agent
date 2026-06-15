/* Vector 身体控制工具: set_head_angle, set_lift_height, stop */



#include "drivers/tool/tool_vector_common.h"



#include "drivers/tool/tool_registry.h"



#include "drivers/channel/vector/vector_channel.h"







#include <stdio.h>



#include <stdlib.h>



#include <string.h>







#include "cjson.h"



#include "linux/printk.h"



static err_t tool_robot_set_head_angle_execute(const char *input_json, char *output, size_t output_size)



{



    mcp_client_t *mcp = tool_get_mcp(output, output_size);



    if (!mcp) return ERR_FAIL;



    cJSON *in = cJSON_Parse(input_json);



    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return ERR_INVALID_ARG; }



    cJSON *a = cJSON_GetObjectItem(in, "angle_rad");



    double angle = a && cJSON_IsNumber(a) ? a->valuedouble : 0.0;



    cJSON_Delete(in);







    cJSON *args = cJSON_CreateObject();



    if (!args) return ERR_NO_MEM;



    cJSON_AddNumberToObject(args, "angle_rad", angle);



    cJSON_AddNumberToObject(args, "speed_rad_per_sec", 2.0);



    return call_mcp_with_args(mcp, "robot_set_head_angle", args, output, output_size);



}







static const struct tool s_head_angle = {



    .name = "robot_set_head_angle",



    .description = "控制 Vector 头部倾斜角度。angle_rad: 角度(弧度), 0=正前, 负=低头, 正=抬头。范围约 -0.38 到 0.68。",



    .input_schema_json =



        "{\"type\":\"object\","



        "\"properties\":{\"angle_rad\":{\"type\":\"number\",\"description\":\"头部角度(弧度)\"}},"



        "\"required\":[\"angle_rad\"]}",



    .execute = tool_robot_set_head_angle_execute,



};







const struct tool *tool_robot_set_head_angle_definition(void) { return &s_head_angle; }







/* ---- Set Lift Height ---- */



static err_t tool_robot_set_lift_height_execute(const char *input_json, char *output, size_t output_size)



{



    mcp_client_t *mcp = tool_get_mcp(output, output_size);



    if (!mcp) return ERR_FAIL;



    cJSON *in = cJSON_Parse(input_json);



    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return ERR_INVALID_ARG; }



    cJSON *h = cJSON_GetObjectItem(in, "height_mm");



    double height = h && cJSON_IsNumber(h) ? h->valuedouble : 50.0;



    cJSON_Delete(in);







    cJSON *args = cJSON_CreateObject();



    if (!args) return ERR_NO_MEM;



    cJSON_AddNumberToObject(args, "height_mm", height);



    cJSON_AddNumberToObject(args, "speed_rad_per_sec", 2.0);



    return call_mcp_with_args(mcp, "robot_set_lift_height", args, output, output_size);



}







static const struct tool s_lift_height = {



    .name = "robot_set_lift_height",



    .description = "控制 Vector 升降臂高度。height_mm: 高度 mm, 0=最低, 90=最高。",



    .input_schema_json =



        "{\"type\":\"object\","



        "\"properties\":{\"height_mm\":{\"type\":\"number\",\"description\":\"高度 mm (0~90)\"}},"



        "\"required\":[\"height_mm\"]}",



    .execute = tool_robot_set_lift_height_execute,



};







const struct tool *tool_robot_set_lift_height_definition(void) { return &s_lift_height; }







/* ---- Stop All Motors ---- */



static err_t tool_robot_stop_execute(const char *input_json, char *output, size_t output_size)



{



    (void)input_json;



    mcp_client_t *mcp = tool_get_mcp(output, output_size);



    if (!mcp) return ERR_FAIL;



    return mcp_client_call_tool(mcp, "robot_stop", "{}", output, output_size);



}







static const struct tool s_stop = {



    .name = "robot_stop",



    .description = "立即停止 Vector 所有电机(轮子、头部、升降臂)。",



    .input_schema_json = "{\"type\":\"object\"}",



    .execute = tool_robot_stop_execute,



};







const struct tool *tool_robot_stop_definition(void) { return &s_stop; }


static int head_angle_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_head_angle_device = {
    .name = "robot_set_head_angle",
    .description = "控制 Vector 头部倾斜角度。angle_rad: 角度(弧度), 0=正前, 负=低头, 正=抬头。范围约 -0.38 到 0.68。",
    .input_schema_json = "{\"type\":\"object\",",
};

static struct tool_driver s_head_angle_driver = {
    .name = "robot_set_head_angle",
    .probe = head_angle_tool_probe,
    .execute = tool_robot_set_head_angle_execute,
};

const struct tool_device *tool_head_angle_device(void)
{
    return &s_head_angle_device;
}

const struct tool_driver *tool_head_angle_driver(void)
{
    return &s_head_angle_driver;
}


static int lift_height_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_lift_height_device = {
    .name = "robot_set_lift_height",
    .description = "控制 Vector 升降臂高度。height_mm: 高度 mm, 0=最低, 90=最高。",
    .input_schema_json = "{\"type\":\"object\",",
};

static struct tool_driver s_lift_height_driver = {
    .name = "robot_set_lift_height",
    .probe = lift_height_tool_probe,
    .execute = tool_robot_set_lift_height_execute,
};

const struct tool_device *tool_lift_height_device(void)
{
    return &s_lift_height_device;
}

const struct tool_driver *tool_lift_height_driver(void)
{
    return &s_lift_height_driver;
}


static int stop_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_stop_device = {
    .name = "robot_stop",
    .description = "立即停止 Vector 所有电机(轮子、头部、升降臂)。",
    .input_schema_json = "{\"type\":\"object\"}",
};

static struct tool_driver s_stop_driver = {
    .name = "robot_stop",
    .probe = stop_tool_probe,
    .execute = tool_robot_stop_execute,
};

const struct tool_device *tool_stop_device(void)
{
    return &s_stop_device;
}

const struct tool_driver *tool_stop_driver(void)
{
    return &s_stop_driver;
}
