#pragma once

#include "list.h"
#include "driver.h"
#include <stddef.h>

/* 四条总线全局实例 */
extern struct bus_type *tool_bus;
extern struct bus_type *mcp_bus;
extern struct bus_type *channel_bus;
extern struct bus_type *llm_bus;

struct bus_type {
    const char *name;
    int (*match)(struct device *dev, struct driver *drv);
    struct list_head devices;
    struct list_head drivers;
};

struct dependency {
    const char *bus_name;
    const char *dev_name;
};

struct device {
    const char *name;
    struct bus_type *bus;
    struct dependency *dependencies;
    int dep_count;
    void *data;
    struct driver *drv;
    struct list_head bus_node;
};

struct bus_type *bus_create(const char *name,
                            int (*match)(struct device *dev, struct driver *drv));
void bus_destroy(struct bus_type *bus);

int device_register(struct device *dev, struct bus_type *bus);
void device_unregister(struct device *dev);

int bus_probe(struct bus_type *bus, struct device *dev);
void bus_probe_all(struct bus_type *bus);
void bus_reprobe(struct bus_type *bus, const char *dev_name);

int bus_device_exists(struct bus_type *bus, const char *name);
struct device *bus_find_device(struct bus_type *bus, const char *name);
