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
#include "drivers/tool/tool_log.h"
#include "drivers/tool/tool_skills.h"
#include "drivers/tool/tool_session_search.h"
#include "drivers/tool/tool_vector_common.h"
#include "drivers/tool/tool_custom.h"

#include <string.h>
#include <stdio.h>
#include "bus.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "cjson.h"
#include "linux/slab.h"
#include "linux/bus.h"
#define MAX_TOOLS 32

static struct tool s_tools[MAX_TOOLS];
static int s_tool_count = 0;
typedef struct {
    struct list_head list;
    struct tool tool;
} dynamic_tool_node_t;

static dynamic_tool_node_t s_dynamic_tools[TOOL_REGISTRY_MAX_DYNAMIC];
static LIST_HEAD(s_dynamic_tool_list);
static int s_dynamic_count = 0;
static char *s_tools_json = NULL;          /* 缓存的完整工具数组字符串 */
static char *s_base_tools_json = NULL;     /* 缓存的不含机器人控制工具数组字符串 */

static bool is_vector_tool_name(const char *name)
{
    return name && strncmp(name, "robot_", 6) == 0;
}

static void register_tool(const struct tool *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        pr_err("Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    pr_info("Registered tool: %s", tool->name);

    /* 同时在 tool_bus 上注册 device */
    if (tool_bus) {
        struct device *dev = kmalloc(sizeof(*dev), GFP_KERNEL);
        if (dev) {
            memset(dev, 0, sizeof(*dev));
            dev->name = tool->name;
            dev->data = (void *)&s_tools[s_tool_count - 1];
            device_register(dev, tool_bus);
        }
    }
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
    dynamic_tool_node_t *node;
    list_for_each_entry(node, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (strcmp(node->tool.name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void add_tool_json(cJSON *arr, const struct tool *def)
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

    dynamic_tool_node_t *node;
    list_for_each_entry(node, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (!include_vector_tools && is_vector_tool_name(node->tool.name)) {
            continue;
        }

        add_tool_json(arr, &node->tool);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

static void build_tools_json(void)
{
    kfree(s_tools_json);
    kfree(s_base_tools_json);
    s_tools_json = build_tools_json_filtered(true);
    s_base_tools_json = build_tools_json_filtered(false);

    pr_info("Tools JSON built (%d static, %d dynamic)", s_tool_count, s_dynamic_count);
}

err_t tool_registry_init(void)
{
    s_tool_count = 0;
    s_dynamic_count = 0;
    INIT_LIST_HEAD(&s_dynamic_tool_list);
    for (int i = 0; i < TOOL_REGISTRY_MAX_DYNAMIC; i++) {
        INIT_LIST_HEAD(&s_dynamic_tools[i].list);
        memset(&s_dynamic_tools[i].tool, 0, sizeof(s_dynamic_tools[i].tool));
    }

    /* 在 tool_bus 上注册通用工具驱动 */
    if (tool_bus) {
        driver_register(&tool_weather_driver()->drv, tool_bus);
        driver_register(&tool_get_time_driver()->drv, tool_bus);
        driver_register(&tool_apply_patch_driver()->drv, tool_bus);
        driver_register(&tool_restore_file_driver()->drv, tool_bus);
        driver_register(&tool_todo_driver()->drv, tool_bus);
        driver_register(&tool_work_item_driver()->drv, tool_bus);
        driver_register(&tool_webfetch_driver()->drv, tool_bus);
        driver_register(&tool_log_driver()->drv, tool_bus);
        driver_register(&tool_skills_driver()->drv, tool_bus);
        driver_register(&tool_session_search_driver()->drv, tool_bus);
        driver_register(&tool_cron_driver()->drv, tool_bus);
        driver_register(&tool_terminal_driver()->drv, tool_bus);
        driver_register(&tool_robot_drive_straight_driver()->drv, tool_bus);
        driver_register(&tool_robot_turn_in_place_driver()->drv, tool_bus);
        driver_register(&tool_robot_drive_wheels_driver()->drv, tool_bus);
        driver_register(&tool_robot_set_head_angle_driver()->drv, tool_bus);
        driver_register(&tool_robot_set_lift_height_driver()->drv, tool_bus);
        driver_register(&tool_robot_stop_driver()->drv, tool_bus);
        driver_register(&tool_robot_set_volume_driver()->drv, tool_bus);
        driver_register(&tool_robot_drive_on_charger_driver()->drv, tool_bus);
        driver_register(&tool_robot_drive_off_charger_driver()->drv, tool_bus);
        driver_register(&tool_robot_play_animation_driver()->drv, tool_bus);
        driver_register(&tool_robot_get_battery_driver()->drv, tool_bus);
    }

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
    register_tool(tool_log_definition());

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

    /* 加载自定义工具 (spiffs_data/custom_tools.json) */
    tool_custom_load_default();

    pr_info("Tool registry initialized");
    return 0;
}

err_t tool_registry_register_dynamic(const struct tool *tool)
{
    if (!tool || !tool->name || !tool->name[0] || !tool->description || !tool->input_schema_json || !tool->execute) {
        return ERR_INVALID_ARG;
    }
    if (s_dynamic_count >= TOOL_REGISTRY_MAX_DYNAMIC) {
        pr_err("Dynamic tool registry full");
        return ERR_NO_MEM;
    }
    if (tool_name_exists(tool->name)) {
        pr_warn("Dynamic tool name already registered: %s", tool->name);
        return ERR_INVALID_STATE;
    }

    dynamic_tool_node_t *slot = NULL;
    for (int i = 0; i < TOOL_REGISTRY_MAX_DYNAMIC; i++) {
        if (!s_dynamic_tools[i].tool.name) {
            slot = &s_dynamic_tools[i];
            break;
        }
    }
    if (!slot) {
        return ERR_NO_MEM;
    }

    slot->tool = *tool;
    list_add(&slot->list, &s_dynamic_tool_list);
    s_dynamic_count++;
    build_tools_json();
    pr_info("Registered dynamic tool: %s", tool->name);
    return 0;
}

err_t tool_registry_unregister_dynamic(const char *tool_name)
{
    if (!tool_name || !tool_name[0]) {
        return ERR_INVALID_ARG;
    }

    dynamic_tool_node_t *node, *next;
    list_for_each_entry_safe(node, next, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (strcmp(node->tool.name, tool_name) == 0) {
            list_del(&node->list);
            memset(&node->tool, 0, sizeof(node->tool));
            s_dynamic_count--;
            build_tools_json();
            pr_info("Unregistered dynamic tool: %s", tool_name);
            return 0;
        }
    }

    return ERR_NOT_FOUND;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

const char *tool_registry_get_tools_json_for_channel(const char *channel)
{
    if (channel &&
        (strcmp(channel, CHAN_VECTOR) == 0 ||
         strcmp(channel, CHAN_VOICE) == 0)) {
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
           (strcmp(channel, CHAN_VECTOR) == 0 ||
            strcmp(channel, CHAN_VOICE) == 0);
}

err_t tool_registry_execute(const char *name, const char *input_json,
                                 char *output, size_t output_size)
{
    /* 优先从 tool_bus 查找 */
    if (tool_bus) {
        struct device *dev = bus_find_device(tool_bus, name);
        if (dev && dev->drv) {
            /* 先检查是否自定义 tool */
            err_t custom_err = tool_custom_execute(name, input_json, output, output_size);
            if (custom_err != ERR_NOT_FOUND) return custom_err;

            struct tool_driver *tdrv = container_of(dev->drv, struct tool_driver, drv);
            pr_info("Executing tool via bus: %s", name);
            return tdrv->execute(input_json, output, output_size);
        }
    }

    /* fallback: 搜索静态数组 */
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            pr_info("Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    dynamic_tool_node_t *node;
    list_for_each_entry(node, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (strcmp(node->tool.name, name) == 0) {
            pr_info("Executing dynamic tool: %s", name);
            return node->tool.execute(input_json, output, output_size);
        }
    }

    pr_warn("Unknown tool: %s", name);
    snprintf(output, output_size, "错误：未知工具 '%s'", name);
    return ERR_NOT_FOUND;
}

err_t tool_registry_execute_for_channel(const char *channel,
                                             const char *name,
                                             const char *input_json,
                                             char *output,
                                             size_t output_size)
{
    if (!output || output_size == 0 || !name) {
        return ERR_INVALID_ARG;
    }
    if (!channel_allows_tool(channel, name)) {
        pr_warn("Tool blocked by channel policy: channel=%s tool=%s", channel ? channel : "(none)", name);
        snprintf(output, output_size,
                 "错误：工具 '%s' 仅允许在 vector/voice 通道使用，当前通道为 '%s'",
                 name, channel ? channel : "");
        return ERR_INVALID_STATE;
    }
    return tool_registry_execute(name, input_json, output, output_size);
}
