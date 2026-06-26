#include "drivers/tool/tool_builtin_bus.h"

#include "drivers/tool/tool_types.h"
#include "drivers/tool/tool_weather.h"
#include "drivers/tool/tool_get_time.h"
#include "drivers/tool/tool_files.h"
#include "drivers/tool/tool_cron.h"
#include "drivers/tool/tool_system.h"
#include "drivers/tool/tool_todo.h"
#include "drivers/tool/tool_work_item.h"
#include "drivers/tool/tool_webfetch.h"
#include "drivers/tool/tool_log.h"
#include "drivers/tool/tool_skills.h"
#include "drivers/tool/tool_session_search.h"
#include "drivers/tool/tool_vector_common.h"
#include "drivers/tool/tool_delegate.h"

#include "bus.h"
#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <string.h>

#define BUILTIN_TOOLS_MAX 32

static struct tool_device s_builtin_tool_devices[BUILTIN_TOOLS_MAX];
static struct device s_builtin_bus_devices[BUILTIN_TOOLS_MAX];
static int s_builtin_tool_count = 0;

static void register_builtin_driver(const struct tool_driver *driver)
{
    if (tool_bus && driver) {
        driver_register((struct driver *)&driver->drv, tool_bus);
    }
}

static void register_builtin_tool(const struct tool *tool)
{
    struct device *dev;

    if (!tool || !tool->name || !tool->description || !tool->input_schema_json) {
        return;
    }
    if (s_builtin_tool_count >= BUILTIN_TOOLS_MAX) {
        pr_err("Builtin tool registry full");
        return;
    }

    s_builtin_tool_devices[s_builtin_tool_count].name = tool->name;
    s_builtin_tool_devices[s_builtin_tool_count].description = tool->description;
    s_builtin_tool_devices[s_builtin_tool_count].input_schema_json = tool->input_schema_json;
    pr_info("Registered tool: %s", tool->name);

    if (tool_bus) {
        dev = &s_builtin_bus_devices[s_builtin_tool_count];
        memset(dev, 0, sizeof(*dev));
        dev->name = tool->name;
        dev->data = &s_builtin_tool_devices[s_builtin_tool_count];
        device_register(dev, tool_bus);
    }

    s_builtin_tool_count++;
}

err_t tool_builtin_bus_init(void)
{
    register_builtin_driver(tool_weather_driver());
    register_builtin_driver(tool_get_time_driver());
    register_builtin_driver(tool_files_driver());
    register_builtin_driver(tool_apply_patch_driver());
    register_builtin_driver(tool_restore_file_driver());
    register_builtin_driver(tool_todo_driver());
    register_builtin_driver(tool_work_item_driver());
    register_builtin_driver(tool_webfetch_driver());
    register_builtin_driver(tool_log_driver());
    register_builtin_driver(tool_skills_driver());
    register_builtin_driver(tool_session_search_driver());
    register_builtin_driver(tool_cron_driver());
    register_builtin_driver(tool_terminal_driver());
    register_builtin_driver(tool_robot_drive_straight_driver());
    register_builtin_driver(tool_robot_turn_in_place_driver());
    register_builtin_driver(tool_robot_drive_wheels_driver());
    register_builtin_driver(tool_robot_set_head_angle_driver());
    register_builtin_driver(tool_robot_set_lift_height_driver());
    register_builtin_driver(tool_robot_stop_driver());
    register_builtin_driver(tool_robot_set_volume_driver());
    register_builtin_driver(tool_robot_drive_on_charger_driver());
    register_builtin_driver(tool_robot_drive_off_charger_driver());
    register_builtin_driver(tool_robot_play_animation_driver());
    register_builtin_driver(tool_robot_get_battery_driver());
    register_builtin_driver(tool_delegate_driver());

    tool_weather_init();

    register_builtin_tool(tool_weather_definition());
    register_builtin_tool(tool_get_time_definition());
    register_builtin_tool(tool_files_definition());
    register_builtin_tool(tool_apply_patch_definition());
    register_builtin_tool(tool_restore_file_definition());
    register_builtin_tool(tool_todo_definition());
    register_builtin_tool(tool_work_item_definition());
    register_builtin_tool(tool_webfetch_definition());
    register_builtin_tool(tool_log_definition());
    register_builtin_tool(tool_skills_definition());
    register_builtin_tool(tool_session_search_definition());
    register_builtin_tool(tool_cron_definition());
    register_builtin_tool(tool_terminal_definition());

    tool_vector_init();
    register_builtin_tool(tool_robot_drive_straight_definition());
    register_builtin_tool(tool_robot_turn_in_place_definition());
    register_builtin_tool(tool_robot_drive_wheels_definition());
    register_builtin_tool(tool_robot_set_head_angle_definition());
    register_builtin_tool(tool_robot_set_lift_height_definition());
    register_builtin_tool(tool_robot_stop_definition());
    register_builtin_tool(tool_robot_set_volume_definition());
    register_builtin_tool(tool_robot_drive_on_charger_definition());
    register_builtin_tool(tool_robot_drive_off_charger_definition());
    register_builtin_tool(tool_robot_play_animation_definition());
    register_builtin_tool(tool_robot_get_battery_definition());
    register_builtin_tool(tool_delegate_definition());

    return 0;
}
