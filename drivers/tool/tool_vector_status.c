/* Vector 状态 + 充电工具 */

#include "drivers/tool/tool_vector_common.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/channel/vector/vector_channel.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "linux/printk.h"
err_t tool_vector_init(void) { return 0; }

/* ---- Get Battery ---- */
static err_t tool_robot_get_battery_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return ERR_FAIL;
    return mcp_client_call_tool(mcp, "robot_get_battery", "{}", output, output_size);
}

static const struct tool s_get_battery = {
    .name = "robot_get_battery",
    .description = "获取 Vector 当前电池状态(电压、电量、是否在充电)。",
    .input_schema_json = "{\"type\":\"object\"}",
    .execute = tool_robot_get_battery_execute,
};
const struct tool *tool_robot_get_battery_definition(void) { return &s_get_battery; }

/* ---- Drive On Charger ---- */
static err_t tool_robot_drive_on_charger_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return ERR_FAIL;
    return mcp_client_call_tool(mcp, "robot_drive_on_charger", "{}", output, output_size);
}

static const struct tool s_drive_on_charger = {
    .name = "robot_drive_on_charger",
    .description = "让 Vector 自动回到充电座并开始充电。机器人会自动定位充电座。",
    .input_schema_json = "{\"type\":\"object\"}",
    .execute = tool_robot_drive_on_charger_execute,
};
const struct tool *tool_robot_drive_on_charger_definition(void) { return &s_drive_on_charger; }

/* ---- Drive Off Charger ---- */
static err_t tool_robot_drive_off_charger_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    mcp_client_t *mcp = tool_get_mcp(output, output_size);
    if (!mcp) return ERR_FAIL;
    return mcp_client_call_tool(mcp, "robot_drive_off_charger", "{}", output, output_size);
}

static const struct tool s_drive_off_charger = {
    .name = "robot_drive_off_charger",
    .description = "让 Vector 离开充电座。",
    .input_schema_json = "{\"type\":\"object\"}",
    .execute = tool_robot_drive_off_charger_execute,
};
const struct tool *tool_robot_drive_off_charger_definition(void) { return &s_drive_off_charger; }
