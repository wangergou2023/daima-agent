#pragma once

#include "init.h"
#include "list.h"

struct bus_type;
struct device;

struct driver {
    const char *name;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    void *priv;
    struct list_head node;
};

int driver_register(struct driver *drv, struct bus_type *bus);
void driver_unregister(struct driver *drv);
