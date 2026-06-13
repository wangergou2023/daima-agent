/* 工具注册与执行分发。 */

#include "tool_registry.h"
#include "drivers/tool/tool_weather.h"
#include "drivers/tool/tool_get_time.h"
#include "drivers/tool/tool_files.h"
#include "drivers/tool/tool_cron.h"
#include "drivers/tool/tool_system.h"
#include "drivers/tool/tool_todo.h"
#include "drivers/tool/tool_work_item.h"
#include "drivers/tool/tool_webfetch.h"
#include "drivers/tool/tool_daima_log.h"
#include "drivers/tool/tool_skills.h"
#include "drivers/tool/tool_session_search.h"
#include "drivers/tool/tool_vector_common.h"

#include <string.h>
#include <stdio.h>
#include "core/bus.h"
#include "core/log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 32

static daima_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static daima_tool_t s_dynamic_tools[TOOL_REGISTRY_MAX_DYNAMIC];
static int s_dynamic_count = 0;
static char *s_tools_json = NULL;          /* 缓存的完整工具数组字符串 */
static char *s_base_tools_json = NULL;     /* 缓存的不含机器人控制工具数组字符串 */

static bool is_vector_tool_name(const char *name)
{
    return name && strncmp(name, "robot_", 6) == 0;
}

static void register_tool(const daima_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        DAIMA_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    DAIMA_LOGI(TAG, "Registered tool: %s", tool->name);
}

static bool tool_name_exists(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return true;
        }
    }
    for (int i = 0; i < s_dynamic_count; i++) {
        if (strcmp(s_dynamic_tools[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void add_tool_json(cJSON *arr, const daima_tool_t *def)
{
    if (!arr || !def) {
        return;
    }

    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", def->name);
    cJSON_AddStringToObject(tool, "description", def->description);

    cJSON *schema = cJSON_Parse(def->input_schema_json);
    if (schema) {
        cJSON_AddItemToObject(tool, "input_schema", schema);
    }

    cJSON_AddItemToArray(arr, tool);
}

static char *build_tools_json_filtered(bool include_vector_tools)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        if (!include_vector_tools && is_vector_tool_name(s_tools[i].name)) {
            continue;
        }

        add_tool_json(arr, &s_tools[i]);
    }

    for (int i = 0; i < s_dynamic_count; i++) {
        if (!include_vector_tools && is_vector_tool_name(s_dynamic_tools[i].name)) {
            continue;
        }

        add_tool_json(arr, &s_dynamic_tools[i]);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

static void build_tools_json(void)
{
    free(s_tools_json);
    free(s_base_tools_json);
    s_tools_json = build_tools_json_filtered(true);
    s_base_tools_json = build_tools_json_filtered(false);

    DAIMA_LOGI(TAG, "Tools JSON built (%d static, %d dynamic)", s_tool_count, s_dynamic_count);
}

daima_err_t tool_registry_init(void)
{
    s_tool_count = 0;
    s_dynamic_count = 0;

    tool_weather_init();

    register_tool(tool_weather_definition());
    register_tool(tool_get_time_definition());

    /* 注册文件工具 */
    register_tool(tool_files_definition());
    register_tool(tool_apply_patch_definition());
    register_tool(tool_restore_file_definition());

    /* 注册 todo */
    register_tool(tool_todo_definition());
    register_tool(tool_work_item_definition());
    register_tool(tool_webfetch_definition());
    register_tool(tool_daima_log_definition());

    register_tool(tool_skills_definition());

    /* 注册 session_search */
    register_tool(tool_session_search_definition());

    register_tool(tool_cron_definition());

    /* 注册 terminal */
    register_tool(tool_terminal_definition());

    /* 注册 Vector 机器人工具；是否暴露给模型由当前 channel 决定 */
    tool_vector_init();
    register_tool(tool_robot_drive_straight_definition());
    register_tool(tool_robot_turn_in_place_definition());
    register_tool(tool_robot_drive_wheels_definition());
    register_tool(tool_robot_set_head_angle_definition());
    register_tool(tool_robot_set_lift_height_definition());
    register_tool(tool_robot_stop_definition());
    register_tool(tool_robot_set_volume_definition());
    register_tool(tool_robot_drive_on_charger_definition());
    register_tool(tool_robot_drive_off_charger_definition());
    register_tool(tool_robot_play_animation_definition());
    register_tool(tool_robot_get_battery_definition());

    build_tools_json();

    DAIMA_LOGI(TAG, "Tool registry initialized");
    return DAIMA_OK;
}

daima_err_t tool_registry_register_dynamic(const daima_tool_t *tool)
{
    if (!tool || !tool->name || !tool->name[0] || !tool->description || !tool->input_schema_json || !tool->execute) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (s_dynamic_count >= TOOL_REGISTRY_MAX_DYNAMIC) {
        DAIMA_LOGE(TAG, "Dynamic tool registry full");
        return DAIMA_ERR_NO_MEM;
    }
    if (tool_name_exists(tool->name)) {
        DAIMA_LOGW(TAG, "Dynamic tool name already registered: %s", tool->name);
        return DAIMA_ERR_INVALID_STATE;
    }

    s_dynamic_tools[s_dynamic_count++] = *tool;
    build_tools_json();
    DAIMA_LOGI(TAG, "Registered dynamic tool: %s", tool->name);
    return DAIMA_OK;
}

daima_err_t tool_registry_unregister_dynamic(const char *tool_name)
{
    if (!tool_name || !tool_name[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_dynamic_count; i++) {
        if (strcmp(s_dynamic_tools[i].name, tool_name) == 0) {
            for (int j = i; j < s_dynamic_count - 1; j++) {
                s_dynamic_tools[j] = s_dynamic_tools[j + 1];
            }
            s_dynamic_count--;
            memset(&s_dynamic_tools[s_dynamic_count], 0, sizeof(s_dynamic_tools[s_dynamic_count]));
            build_tools_json();
            DAIMA_LOGI(TAG, "Unregistered dynamic tool: %s", tool_name);
            return DAIMA_OK;
        }
    }

    return DAIMA_ERR_NOT_FOUND;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

const char *tool_registry_get_tools_json_for_channel(const char *channel)
{
    if (channel &&
        (strcmp(channel, DAIMA_CHAN_VECTOR) == 0 ||
         strcmp(channel, DAIMA_CHAN_VOICE) == 0)) {
        return s_tools_json;
    }
    return s_base_tools_json ? s_base_tools_json : s_tools_json;
}

static bool channel_allows_tool(const char *channel, const char *tool_name)
{
    if (!is_vector_tool_name(tool_name)) {
        return true;
    }
    return channel &&
           (strcmp(channel, DAIMA_CHAN_VECTOR) == 0 ||
            strcmp(channel, DAIMA_CHAN_VOICE) == 0);
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

    for (int i = 0; i < s_dynamic_count; i++) {
        if (strcmp(s_dynamic_tools[i].name, name) == 0) {
            DAIMA_LOGI(TAG, "Executing dynamic tool: %s", name);
            return s_dynamic_tools[i].execute(input_json, output, output_size);
        }
    }

    DAIMA_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "错误：未知工具 '%s'", name);
    return DAIMA_ERR_NOT_FOUND;
}

daima_err_t tool_registry_execute_for_channel(const char *channel,
                                             const char *name,
                                             const char *input_json,
                                             char *output,
                                             size_t output_size)
{
    if (!output || output_size == 0 || !name) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!channel_allows_tool(channel, name)) {
        DAIMA_LOGW(TAG, "Tool blocked by channel policy: channel=%s tool=%s",
                  channel ? channel : "(none)", name);
        snprintf(output, output_size,
                 "错误：工具 '%s' 仅允许在 vector/voice 通道使用，当前通道为 '%s'",
                 name, channel ? channel : "");
        return DAIMA_ERR_INVALID_STATE;
    }
    return tool_registry_execute(name, input_json, output, output_size);
}
