/* 动态工具注册到 tool_bus。 */

#include "drivers/tool/tool_dynamic_bus.h"
#include "drivers/tool/tool_custom.h"

#include <string.h>
#include "bus.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/bus.h"

typedef struct {
    struct list_head list;
    struct tool_device tool_dev;
    struct tool_driver tool_drv;
    struct device bus_dev;
    bool bus_registered;
} dynamic_tool_node_t;

static dynamic_tool_node_t s_dynamic_tools[TOOL_DYNAMIC_BUS_MAX];
static LIST_HEAD(s_dynamic_tool_list);
static int s_dynamic_count = 0;

static int dynamic_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static bool tool_name_exists(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }

    dynamic_tool_node_t *node;
    list_for_each_entry(node, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (strcmp(node->tool_dev.name, name) == 0) {
            return true;
        }
    }

    for (int i = 0; i < tool_custom_count(); i++) {
        const struct tool_custom_meta *meta = tool_custom_get(i);
        if (meta && meta->name && strcmp(meta->name, name) == 0) {
            return true;
        }
    }

    return false;
}

err_t tool_dynamic_bus_register(const struct tool *tool)
{
    if (!tool || !tool->name || !tool->name[0] || !tool->description || !tool->input_schema_json || !tool->execute) {
        return ERR_INVALID_ARG;
    }
    if (s_dynamic_count >= TOOL_DYNAMIC_BUS_MAX) {
        pr_err("Dynamic tool registry full");
        return ERR_NO_MEM;
    }
    if (tool_name_exists(tool->name)) {
        pr_warn("Dynamic tool name already registered: %s", tool->name);
        return ERR_INVALID_STATE;
    }

    dynamic_tool_node_t *slot = NULL;
    for (int i = 0; i < TOOL_DYNAMIC_BUS_MAX; i++) {
        if (!s_dynamic_tools[i].tool_dev.name) {
            slot = &s_dynamic_tools[i];
            break;
        }
    }
    if (!slot) {
        return ERR_NO_MEM;
    }

    slot->tool_dev.name = tool->name;
    slot->tool_dev.description = tool->description;
    slot->tool_dev.input_schema_json = tool->input_schema_json;
    slot->tool_drv.drv.name = tool->name;
    slot->tool_drv.drv.probe = dynamic_tool_probe;
    slot->tool_drv.execute = tool->execute;
    INIT_LIST_HEAD(&slot->tool_drv.drv.node);
    memset(&slot->bus_dev, 0, sizeof(slot->bus_dev));
    slot->bus_dev.name = tool->name;
    slot->bus_dev.data = &slot->tool_dev;
    list_add(&slot->list, &s_dynamic_tool_list);
    s_dynamic_count++;

    if (tool_bus) {
        if (driver_register(&slot->tool_drv.drv, tool_bus) != 0 ||
            device_register(&slot->bus_dev, tool_bus) != 0) {
            if (slot->tool_drv.drv.bus) {
                driver_unregister(&slot->tool_drv.drv);
            }
            list_del(&slot->list);
            memset(&slot->tool_dev, 0, sizeof(slot->tool_dev));
            memset(&slot->tool_drv, 0, sizeof(slot->tool_drv));
            memset(&slot->bus_dev, 0, sizeof(slot->bus_dev));
            s_dynamic_count--;
            return ERR_NO_MEM;
        }
        slot->bus_registered = true;
    }

    pr_info("Registered dynamic tool: %s", tool->name);
    return 0;
}

err_t tool_dynamic_bus_unregister(const char *tool_name)
{
    if (!tool_name || !tool_name[0]) {
        return ERR_INVALID_ARG;
    }

    dynamic_tool_node_t *node, *next;
    list_for_each_entry_safe(node, next, &s_dynamic_tool_list, list, dynamic_tool_node_t) {
        if (strcmp(node->tool_dev.name, tool_name) == 0) {
            if (node->bus_registered) {
                device_unregister(&node->bus_dev);
                driver_unregister(&node->tool_drv.drv);
                node->bus_registered = false;
            }
            list_del(&node->list);
            memset(&node->tool_dev, 0, sizeof(node->tool_dev));
            memset(&node->tool_drv, 0, sizeof(node->tool_drv));
            memset(&node->bus_dev, 0, sizeof(node->bus_dev));
            INIT_LIST_HEAD(&node->list);
            s_dynamic_count--;
            pr_info("Unregistered dynamic tool: %s", tool_name);
            return 0;
        }
    }

    return ERR_NOT_FOUND;
}
