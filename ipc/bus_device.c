#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"

#include <string.h>

static int default_name_match(struct device *dev, struct driver *drv)
{
    if (!dev->name || !drv->name) {
        return -1;
    }

    return strcmp(dev->name, drv->name) == 0 ? 0 : -1;
}

static int bus_match(struct bus_type *bus, struct device *dev, struct driver *drv)
{
    if (bus->match) {
        return bus->match(dev, drv);
    }

    return default_name_match(dev, drv);
}

static void bus_probe_device(struct bus_type *bus, struct device *dev)
{
    struct list_head *pos;

    if (!bus || !dev || dev->drv) {
        return;
    }

    for (pos = bus->drivers.next; pos != &bus->drivers; pos = pos->next) {
        struct driver *drv = container_of(pos, struct driver, node);
        int ret;

        if (bus_match(bus, dev, drv) != 0) {
            continue;
        }

        if (drv->probe) {
            ret = drv->probe(dev);
            if (ret != 0) {
                pr_warn("bus: %s: %s probe failed for %s (%d)",
                        bus->name, dev->name, drv->name, ret);
                return;
            }
        }

        dev->drv = drv;
        pr_info("bus: %s: %s bound to %s%s",
                bus->name,
                dev->name,
                drv->name,
                drv->probe ? "" : " (no probe)");
        return;
    }

    pr_debug("bus: %s: no driver found for %s", bus->name, dev->name);
}

struct bus_type *bus_create(const char *name,
                            int (*match)(struct device *dev, struct driver *drv))
{
    struct bus_type *bus = kmalloc(sizeof(*bus), GFP_KERNEL);

    if (!bus) {
        pr_err("bus_create: no memory for %s", name ? name : "<null>");
        return NULL;
    }

    bus->name = name;
    bus->match = match;
    INIT_LIST_HEAD(&bus->devices);
    INIT_LIST_HEAD(&bus->drivers);

    pr_info("bus: %s created", bus->name ? bus->name : "<unnamed>");
    return bus;
}

void bus_destroy(struct bus_type *bus)
{
    if (!bus) {
        return;
    }

    pr_info("bus: %s destroyed", bus->name ? bus->name : "<unnamed>");
    kfree(bus);
}

int driver_register(struct driver *drv, struct bus_type *bus)
{
    if (!drv || !bus) {
        return -1;
    }

    drv->bus = bus;
    INIT_LIST_HEAD(&drv->node);
    list_add(&drv->node, &bus->drivers);

    pr_info("bus: %s driver registered: %s",
            bus->name ? bus->name : "<unnamed>",
            drv->name ? drv->name : "<unnamed>");

    bus_probe_all(bus);
    return 0;
}

void driver_unregister(struct driver *drv)
{
    struct list_head *pos;
    struct bus_type *bus;

    if (!drv || !drv->bus) {
        return;
    }

    bus = drv->bus;

    for (pos = bus->devices.next; pos != &bus->devices; pos = pos->next) {
        struct device *dev = container_of(pos, struct device, bus_node);

        if (dev->drv != drv) {
            continue;
        }

        if (drv->remove) {
            drv->remove(dev);
        }

        dev->drv = NULL;
    }

    list_del(&drv->node);
    drv->bus = NULL;

    pr_info("bus: %s driver unregistered: %s",
            bus->name ? bus->name : "<unnamed>",
            drv->name ? drv->name : "<unnamed>");
}

int device_register(struct device *dev, struct bus_type *bus)
{
    if (!dev || !bus) {
        return -1;
    }

    dev->bus = bus;
    dev->drv = NULL;
    INIT_LIST_HEAD(&dev->bus_node);
    list_add(&dev->bus_node, &bus->devices);

    pr_info("bus: %s device registered: %s",
            bus->name ? bus->name : "<unnamed>",
            dev->name ? dev->name : "<unnamed>");

    bus_probe(bus, dev);
    return 0;
}

void device_unregister(struct device *dev)
{
    if (!dev || !dev->bus) {
        return;
    }

    if (dev->drv && dev->drv->remove) {
        dev->drv->remove(dev);
    }

    dev->drv = NULL;
    list_del(&dev->bus_node);

    pr_info("bus: %s device unregistered: %s",
            dev->bus->name ? dev->bus->name : "<unnamed>",
            dev->name ? dev->name : "<unnamed>");
    dev->bus = NULL;
}

int bus_probe(struct bus_type *bus, struct device *dev)
{
    if (!bus || !dev) {
        return -1;
    }

    bus_probe_device(bus, dev);
    return dev->drv ? 0 : -1;
}

void bus_probe_all(struct bus_type *bus)
{
    struct list_head *pos;

    if (!bus) {
        return;
    }

    for (pos = bus->devices.next; pos != &bus->devices; pos = pos->next) {
        struct device *dev = container_of(pos, struct device, bus_node);

        if (!dev->drv) {
            bus_probe_device(bus, dev);
        }
    }
}

void bus_reprobe(struct bus_type *bus, const char *dev_name)
{
    struct device *dev;

    if (!bus || !dev_name) {
        return;
    }

    dev = bus_find_device(bus, dev_name);
    if (dev && !dev->drv) {
        bus_probe_device(bus, dev);
    }
}

int bus_device_exists(struct bus_type *bus, const char *name)
{
    return bus_find_device(bus, name) ? 1 : 0;
}

struct device *bus_find_device(struct bus_type *bus, const char *name)
{
    struct list_head *pos;

    if (!bus || !name) {
        return NULL;
    }

    for (pos = bus->devices.next; pos != &bus->devices; pos = pos->next) {
        struct device *dev = container_of(pos, struct device, bus_node);

        if (dev->name && strcmp(dev->name, name) == 0) {
            return dev;
        }
    }

    return NULL;
}
