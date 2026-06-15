/* 总线核心：bus/driver/device 注册与 probe */
#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <string.h>

/*
 * default_name_match - 字符串精确匹配
 * 和 Linux platform_bus 一样，比较 dev->name 和 drv->name
 */
static int default_name_match(struct device *dev, struct driver *drv)
{
    if (!dev->name || !drv->name) return -1;
    return strcmp(dev->name, drv->name) == 0 ? 0 : -1;
}

/* ── 总线生命周期 ────────────────────────── */

struct bus_type *bus_create(const char *name,
                            int (*match)(struct device *dev, struct driver *drv))
{
    struct bus_type *bus = kmalloc(sizeof(*bus), GFP_KERNEL);
    if (!bus) {
        pr_err("bus_create: no memory for %s", name);
        return NULL;
    }
    bus->name = name;
    bus->match = match;
    INIT_LIST_HEAD(&bus->devices);
    INIT_LIST_HEAD(&bus->drivers);
    pr_info("bus: %s created", name);
    return bus;
}

void bus_destroy(struct bus_type *bus)
{
    if (!bus) return;
    pr_info("bus: %s destroyed", bus->name);
    kfree(bus);
}

/* ── 驱动注册 ────────────────────────────── */

int driver_register(struct driver *drv, struct bus_type *bus)
{
    if (!drv || !bus) return -1;
    drv->bus = bus;
    list_add(&drv->node, &bus->drivers);
    pr_info("bus: %s driver registered: %s", bus->name, drv->name);

    /* 重新扫描所有已存在的 device，尝试 bind */
    bus_probe_all(bus);
    return 0;
}

void driver_unregister(struct driver *drv)
{
    if (!drv || !drv->bus) return;
    list_del(&drv->node);
    pr_info("bus: %s driver unregistered: %s", drv->bus->name, drv->name);
}

/* ── 设备注册 ────────────────────────────── */

int device_register(struct device *dev, struct bus_type *bus)
{
    if (!dev || !bus) return -1;
    dev->bus = bus;
    dev->drv = NULL;
    INIT_LIST_HEAD(&dev->bus_node);
    list_add(&dev->bus_node, &bus->devices);
    pr_info("bus: %s device registered: %s", bus->name, dev->name);

    /* 自动 probe */
    return bus_probe(bus, dev);
}

void device_unregister(struct device *dev)
{
    if (!dev || !dev->bus) return;
    if (dev->drv) {
        if (dev->drv->remove)
            dev->drv->remove(dev);
        dev->drv = NULL;
    }
    list_del(&dev->bus_node);
    pr_info("bus: %s device unregistered: %s", dev->bus->name, dev->name);
}

/* ── Probe 流程 ──────────────────────────── */

int bus_probe(struct bus_type *bus, struct device *dev)
{
    if (!bus || !dev) return -1;

    struct driver *drv;
    list_for_each_entry(drv, &bus->drivers, node, struct driver) {
        int match = bus->match ? bus->match(dev, drv) : default_name_match(dev, drv);
        if (match == 0) {
            if (drv->probe) {
                int ret = drv->probe(dev);
                if (ret == 0) {
                    dev->drv = drv;
                    pr_info("bus: %s: %s bound to %s",
                            bus->name, dev->name, drv->name);
                    return 0;
                }
                /* probe 失败：设备保留在总线上，不绑定 */
                pr_warn("bus: %s: %s probe failed for %s (err %d)",
                        bus->name, dev->name, drv->name, ret);
                return ret;
            }
            /* 无 probe 回调，直接绑定 */
            dev->drv = drv;
            pr_info("bus: %s: %s bound to %s (no probe)",
                    bus->name, dev->name, drv->name);
            return 0;
        }
    }

    pr_debug("bus: %s: no driver found for %s", bus->name, dev->name);
    return -1;
}

void bus_probe_all(struct bus_type *bus)
{
    if (!bus) return;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_node, struct device) {
        if (!dev->drv) {
            bus_probe(bus, dev);
        }
    }
}

void bus_reprobe(struct bus_type *bus, const char *dev_name)
{
    if (!bus || !dev_name) return;
    struct device *dev = bus_find_device(bus, dev_name);
    if (dev && !dev->drv) {
        bus_probe(bus, dev);
    }
}

/* ── 查询 ────────────────────────────────── */

struct device *bus_find_device(struct bus_type *bus, const char *name)
{
    if (!bus || !name) return NULL;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_node, struct device) {
        if (dev->name && strcmp(dev->name, name) == 0)
            return dev;
    }
    return NULL;
}

int bus_device_exists(struct bus_type *bus, const char *name)
{
    return bus_find_device(bus, name) != NULL ? 1 : 0;
}
