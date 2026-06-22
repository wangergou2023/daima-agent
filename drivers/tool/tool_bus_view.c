/* 从 tool_bus 读取工具元信息，并构建面向 LLM 的工具 JSON。 */
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_custom.h"

#include "bus.h"
#include "linux/bus.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "cjson.h"

#include <string.h>

static char *s_tools_json;
static char *s_base_tools_json;

static bool is_vector_tool_name(const char *name)
{
    return name && strncmp(name, "robot_", 6) == 0;
}

bool tool_bus_channel_allows_tool(const char *channel, const char *tool_name)
{
    if (!is_vector_tool_name(tool_name)) {
        return true;
    }
    return channel &&
           (strcmp(channel, CHAN_VECTOR) == 0 ||
            strcmp(channel, CHAN_VOICE) == 0);
}

const struct tool_device *tool_bus_get_device(const char *name)
{
    struct device *dev = bus_find_device(tool_bus, name);
    if (!dev || !dev->drv) {
        return NULL;
    }
    return (const struct tool_device *)dev->data;
}

err_t tool_bus_execute(const char *name, const char *input_json, char *output, size_t output_size)
{
    err_t custom_err = tool_custom_execute(name, input_json, output, output_size);
    if (custom_err != ERR_NOT_FOUND) {
        return custom_err;
    }

    struct device *dev = bus_find_device(tool_bus, name);
    if (!dev || !dev->drv) {
        return ERR_NOT_FOUND;
    }

    struct tool_driver *tool_drv = container_of(dev->drv, struct tool_driver, drv);
    if (!tool_drv->execute) {
        return ERR_NOT_FOUND;
    }

    return tool_drv->execute(input_json ? input_json : "{}", output, output_size);
}

err_t tool_bus_execute_for_channel(const char *channel,
                                   const char *name,
                                   const char *input_json,
                                   char *output,
                                   size_t output_size)
{
    if (!output || output_size == 0 || !name) {
        return ERR_INVALID_ARG;
    }
    if (!tool_bus_channel_allows_tool(channel, name)) {
        pr_warn("Tool blocked by channel policy: channel=%s tool=%s",
                channel ? channel : "(none)", name);
        snprintf(output, output_size,
                 "错误：工具 '%s' 仅允许在 vector/voice 通道使用，当前通道为 '%s'",
                 name, channel ? channel : "");
        return ERR_INVALID_STATE;
    }
    return tool_bus_execute(name, input_json, output, output_size);
}

static void add_tool_json(cJSON *arr, const struct tool_device *def)
{
    if (!arr || !def || !def->name || !def->description || !def->input_schema_json) {
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

    if (!tool_bus) {
        char *json = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);
        return json;
    }

    struct list_head *pos;
    for (pos = tool_bus->devices.next; pos != &tool_bus->devices; pos = pos->next) {
        struct device *dev = container_of(pos, struct device, bus_node);
        const struct tool_device *tool = dev->data;
        if (!tool || !dev->drv) {
            continue;
        }
        if (!include_vector_tools && is_vector_tool_name(tool->name)) {
            continue;
        }
        add_tool_json(arr, tool);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

static void rebuild_tools_json(void)
{
    kfree(s_tools_json);
    kfree(s_base_tools_json);
    s_tools_json = build_tools_json_filtered(true);
    s_base_tools_json = build_tools_json_filtered(false);
}

const char *tool_bus_tools_json(void)
{
    rebuild_tools_json();
    return s_tools_json;
}

const char *tool_bus_tools_json_for_channel(const char *channel)
{
    rebuild_tools_json();
    if (channel && tool_bus_channel_allows_tool(channel, "robot_dummy")) {
        return s_tools_json;
    }
    return s_base_tools_json ? s_base_tools_json : s_tools_json;
}
