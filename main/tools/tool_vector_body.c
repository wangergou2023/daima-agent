/* Vector 身体控制工具: set_head_angle, set_lift_height, stop */
#include "tools/tool_vector_common.h"
#include "tools/tool_registry.h"
#include "channels/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "tool_vector_body";

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

/* ---- Set Head Angle ---- */
static daima_err_t tool_robot_set_head_angle_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = get_mcp(output, output_size);
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
    mcp_client_t *mcp = get_mcp(output, output_size);
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
    mcp_client_t *mcp = get_mcp(output, output_size);
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
