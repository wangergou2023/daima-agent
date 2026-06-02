/* Vector 机器人工具 — 共享定义 */
#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 Vector 工具子系统 */
daima_err_t tool_vector_init(void);

/* 工具定义访问器 */
const daima_tool_t *tool_robot_drive_straight_definition(void);
const daima_tool_t *tool_robot_turn_in_place_definition(void);
const daima_tool_t *tool_robot_drive_wheels_definition(void);
const daima_tool_t *tool_robot_set_head_angle_definition(void);
const daima_tool_t *tool_robot_set_lift_height_definition(void);
const daima_tool_t *tool_robot_stop_definition(void);
const daima_tool_t *tool_robot_set_volume_definition(void);
const daima_tool_t *tool_robot_drive_on_charger_definition(void);
const daima_tool_t *tool_robot_drive_off_charger_definition(void);
const daima_tool_t *tool_robot_play_animation_definition(void);
const daima_tool_t *tool_robot_get_battery_definition(void);

#ifdef __cplusplus
}
#endif
