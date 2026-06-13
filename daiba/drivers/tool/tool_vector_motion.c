/* Vector 运动控制工具: drive_straight, turn_in_place, drive_wheels */
#include "drivers/tool/tool_vector_common.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/channel/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "core/log.h"

static const char *TAG = "tool_vector_motion";


/* ---- Drive Straight ---- */
static daima_err_t tool_robot_drive_straight_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) {
        snprintf(output, output_size, "错误：Vector 机器人未连接");
        return DAIMA_FAIL;
    }
    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }

    cJSON *s = cJSON_GetObjectItem(in, "speed_mmps");
    cJSON *d = cJSON_GetObjectItem(in, "dist_mm");
    double speed = s && cJSON_IsNumber(s) ? s->valuedouble : 80.0;
    double dist  = d && cJSON_IsNumber(d) ? d->valuedouble : 100.0;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "speed_mmps", speed);
    cJSON_AddNumberToObject(args, "dist_mm", dist);
    return call_mcp_with_args(mcp, "robot_drive_straight", args, output, output_size);
}

static const daima_tool_t s_drive_straight = {
    .name = "robot_drive_straight",
    .description = "让 Vector 机器人直行前进或后退。speed_mmps: 速度 mm/s (正向前进，负向后腿，推荐 50-200)。dist_mm: 距离 mm。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"speed_mmps\":{\"type\":\"number\",\"description\":\"速度 mm/s (正=前进，负=后退)\"},"
        "\"dist_mm\":{\"type\":\"number\",\"description\":\"距离 mm\"}"
        "},\"required\":[\"speed_mmps\",\"dist_mm\"]}",
    .execute = tool_robot_drive_straight_execute,
};

const daima_tool_t *tool_robot_drive_straight_definition(void) { return &s_drive_straight; }

/* ---- Turn In Place ---- */
static daima_err_t tool_robot_turn_in_place_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) { snprintf(output, output_size, "错误：Vector 机器人未连接"); return DAIMA_FAIL; }

    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }

    cJSON *a = cJSON_GetObjectItem(in, "angle_rad");
    double angle = a && cJSON_IsNumber(a) ? a->valuedouble : 1.5708;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "angle_rad", angle);
    cJSON_AddNumberToObject(args, "speed_rad_per_sec", 2.0);
    cJSON_AddNumberToObject(args, "accel_rad_per_sec2", 10.0);
    return call_mcp_with_args(mcp, "robot_turn_in_place", args, output, output_size);
}

static const daima_tool_t s_turn_in_place = {
    .name = "robot_turn_in_place",
    .description = "让 Vector 机器人原地转向。angle_rad: 角度(弧度), pi/2=右转90°。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"angle_rad\":{\"type\":\"number\",\"description\":\"转向角度(弧度)，正值右转\"}"
        "},\"required\":[\"angle_rad\"]}",
    .execute = tool_robot_turn_in_place_execute,
};

const daima_tool_t *tool_robot_turn_in_place_definition(void) { return &s_turn_in_place; }

/* ---- Drive Wheels ---- */
static daima_err_t tool_robot_drive_wheels_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) { snprintf(output, output_size, "错误：Vector 机器人未连接"); return DAIMA_FAIL; }

    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }

    cJSON *l = cJSON_GetObjectItem(in, "left_mmps");
    cJSON *r = cJSON_GetObjectItem(in, "right_mmps");
    double left  = l && cJSON_IsNumber(l) ? l->valuedouble : 50.0;
    double right = r && cJSON_IsNumber(r) ? r->valuedouble : 50.0;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "left_mmps", left);
    cJSON_AddNumberToObject(args, "right_mmps", right);
    return call_mcp_with_args(mcp, "robot_drive_wheels", args, output, output_size);
}

static const daima_tool_t s_drive_wheels = {
    .name = "robot_drive_wheels",
    .description = "控制 Vector 左右轮速度实现曲线或原地旋转。left_mmps: 左轮速度 mm/s, right_mmps: 右轮速度 mm/s。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"left_mmps\":{\"type\":\"number\",\"description\":\"左轮速度 mm/s\"},"
        "\"right_mmps\":{\"type\":\"number\",\"description\":\"右轮速度 mm/s\"}"
        "},\"required\":[\"left_mmps\",\"right_mmps\"]}",
    .execute = tool_robot_drive_wheels_execute,
};

const daima_tool_t *tool_robot_drive_wheels_definition(void) { return &s_drive_wheels; }
