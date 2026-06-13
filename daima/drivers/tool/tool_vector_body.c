/* Vector 身体控制工具: set_head_angle, set_lift_height, stop */
#include "drivers/tool/tool_vector_common.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/channel/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "linux/printk.h"

static const char *TAG = "tool_vector_body";

static daima_err_t tool_robot_set_head_angle_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;
    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }
    cJSON *a = cJSON_GetObjectItem(in, "angle_rad");
    double angle = a && cJSON_IsNumber(a) ? a->valuedouble : 0.0;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "angle_rad", angle);
    cJSON_AddNumberToObject(args, "speed_rad_per_sec", 2.0);
    return call_mcp_with_args(mcp, "robot_set_head_angle", args, output, output_size);
}

static const daima_tool_t s_head_angle = {
    .name = "robot_set_head_angle",
    .description = "控制 Vector 头部倾斜角度。angle_rad: 角度(弧度), 0=正前, 负=低头, 正=抬头。范围约 -0.38 到 0.68。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"angle_rad\":{\"type\":\"number\",\"description\":\"头部角度(弧度)\"}},"
        "\"required\":[\"angle_rad\"]}",
    .execute = tool_robot_set_head_angle_execute,
};

const daima_tool_t *tool_robot_set_head_angle_definition(void) { return &s_head_angle; }

/* ---- Set Lift Height ---- */
static daima_err_t tool_robot_set_lift_height_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;
    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }
    cJSON *h = cJSON_GetObjectItem(in, "height_mm");
    double height = h && cJSON_IsNumber(h) ? h->valuedouble : 50.0;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "height_mm", height);
    cJSON_AddNumberToObject(args, "speed_rad_per_sec", 2.0);
    return call_mcp_with_args(mcp, "robot_set_lift_height", args, output, output_size);
}

static const daima_tool_t s_lift_height = {
    .name = "robot_set_lift_height",
    .description = "控制 Vector 升降臂高度。height_mm: 高度 mm, 0=最低, 90=最高。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"height_mm\":{\"type\":\"number\",\"description\":\"高度 mm (0~90)\"}},"
        "\"required\":[\"height_mm\"]}",
    .execute = tool_robot_set_lift_height_execute,
};

const daima_tool_t *tool_robot_set_lift_height_definition(void) { return &s_lift_height; }

/* ---- Stop All Motors ---- */
static daima_err_t tool_robot_stop_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;
    return mcp_client_call_tool(mcp, "robot_stop", "{}", output, output_size);
}

static const daima_tool_t s_stop = {
    .name = "robot_stop",
    .description = "立即停止 Vector 所有电机(轮子、头部、升降臂)。",
    .input_schema_json = "{\"type\":\"object\"}",
    .execute = tool_robot_stop_execute,
};

const daima_tool_t *tool_robot_stop_definition(void) { return &s_stop; }
