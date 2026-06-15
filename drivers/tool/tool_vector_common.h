/* Vector 机器人工具 — 共享定义 */
#pragma once

#include "err.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/channel/vector/vector_channel.h"
#include "drivers/channel/vector/mcp_client.h"

#include <stdio.h>
#include <stdlib.h>

#include "cjson.h"
#include "linux/slab.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 Vector 工具子系统 */
err_t tool_vector_init(void);

/* 公共助手：cJSON args → JSON 字符串 → MCP 调用 */
static inline err_t call_mcp_with_args(mcp_client_t *mcp, const char *tool_name, cJSON *args,
                                             char *output, size_t output_size)
{
    char *args_json = cJSON_PrintUnformatted(args);
    if (!args_json) {
        cJSON_Delete(args);
        snprintf(output, output_size, "错误：JSON 序列化失败");
        return ERR_NO_MEM;
    }
    err_t err = mcp_client_call_tool(mcp, tool_name, args_json, output, output_size);
    kfree(args_json);
    cJSON_Delete(args);
    return err;
}

/* 公共助手：获取 MCP 客户端，失败时写错误输出 */
static inline mcp_client_t *tool_get_mcp(char *output, size_t size)
{
    err_t start_err = vector_channel_ensure_started();
    if (start_err != 0) {
        snprintf(output, size, "错误：Vector 机器人启动失败：%s", err_name(start_err));
        return NULL;
    }
    mcp_client_t *m = vector_channel_get_mcp();
    if (!m) snprintf(output, size, "错误：Vector 机器人未连接");
    return m;
}

/* 工具定义访问器 */
const struct tool *tool_robot_drive_straight_definition(void);
const struct tool *tool_robot_turn_in_place_definition(void);
const struct tool *tool_robot_drive_wheels_definition(void);
const struct tool *tool_robot_set_head_angle_definition(void);
const struct tool *tool_robot_set_lift_height_definition(void);
const struct tool *tool_robot_stop_definition(void);
const struct tool *tool_robot_set_volume_definition(void);
const struct tool *tool_robot_drive_on_charger_definition(void);
const struct tool *tool_robot_drive_off_charger_definition(void);
const struct tool *tool_robot_play_animation_definition(void);
const struct tool *tool_robot_get_battery_definition(void);

#ifdef __cplusplus
}
#endif
