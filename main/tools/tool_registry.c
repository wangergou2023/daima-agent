/* 工具注册与执行分发。 */

#include "tool_registry.h"
#include "tools/tool_weather.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_system.h"
#include "tools/tool_todo.h"
#include "tools/tool_skills.h"
#include "tools/tool_session_search.h"
#include "tools/tool_vector_common.h"

#include <string.h>
#include "daima_log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 32

static daima_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* 缓存的 JSON 数组字符串 */

static void register_tool(const daima_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        DAIMA_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    DAIMA_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    DAIMA_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

daima_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    tool_weather_init();

    register_tool(tool_weather_definition());
    register_tool(tool_get_time_definition());

    /* 注册文件工具 */
    register_tool(tool_read_file_definition());
    register_tool(tool_write_file_definition());
    register_tool(tool_edit_file_definition());
    register_tool(tool_patch_definition());
    register_tool(tool_restore_file_definition());
    register_tool(tool_list_dir_definition());
    register_tool(tool_search_files_definition());

    /* 注册 todo */
    register_tool(tool_todo_definition());

    register_tool(tool_skills_list_definition());
    register_tool(tool_skill_view_definition());

    /* 注册 session_search */
    register_tool(tool_session_search_definition());

    register_tool(tool_cron_add_definition());
    register_tool(tool_cron_list_definition());
    register_tool(tool_cron_remove_definition());

    /* 注册 terminal */
    register_tool(tool_terminal_definition());

    /* 注册 Vector 机器人工具 */
    tool_vector_init();
    register_tool(tool_robot_drive_straight_definition());
    register_tool(tool_robot_turn_in_place_definition());
    register_tool(tool_robot_drive_wheels_definition());
    register_tool(tool_robot_set_head_angle_definition());
    register_tool(tool_robot_set_lift_height_definition());
    register_tool(tool_robot_stop_definition());
    register_tool(tool_robot_play_pcm_definition());
    register_tool(tool_robot_set_volume_definition());
    register_tool(tool_robot_drive_on_charger_definition());
    register_tool(tool_robot_drive_off_charger_definition());
    register_tool(tool_robot_play_animation_definition());
    register_tool(tool_robot_get_battery_definition());

    build_tools_json();

    DAIMA_LOGI(TAG, "Tool registry initialized");
    return DAIMA_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

daima_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            DAIMA_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    DAIMA_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "错误：未知工具 '%s'", name);
    return DAIMA_ERR_NOT_FOUND;
}
