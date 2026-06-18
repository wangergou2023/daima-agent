/* 总线设备核心——bus_type/device/driver 生命周期的完整实现。
 *
 * 设计思路：
 * 仿 Linux 内核驱动模型，提供 bus_type（总线类型）、device（设备）、driver（驱动）
 * 三层抽象。每条总线上维护两个链表：devices 和 drivers。注册时：
 *   - driver_register() 将驱动加入 bus->drivers 链表
 *   - device_register() 将设备加入 bus->devices 链表并立即调用 bus_probe()
 * probe 遍历 drivers 链表，通过 match 函数匹配设备与驱动，成功则调用 probe()。
 *
 * 三条实际使用的总线（在 init/bootstrap.c 中创建）：
 *   - tool_bus    : 工具驱动/设备
 *   - channel_bus : 通道驱动/设备（feishu/websocket/vector）
 *   - llm_bus     : LLM 驱动/设备（openai/anthropic）
 *
 * 核心 API：
 *   bus_create/destroy → 总线生命周期
 *   driver_register/unregister → 驱动注册/注销（注销时解绑所有设备）
 *   device_register/unregister → 设备注册/注销（注册时自动 probe）
 *   bus_probe/probe_all → 单设备/全设备扫描绑定
 *   bus_reprobe → 依赖就绪后重新尝试绑定
 *   bus_find_device/bus_device_exists → 设备查找
 */

#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"

#include <string.h>

/* 默认匹配策略：按名称字符串比较（大小写敏感）。 */
static int default_name_match(struct device *dev, struct driver *drv)
{
    if (!dev->name || !drv->name) {
        return -1;
    }

    return strcmp(dev->name, drv->name) == 0 ? 0 : -1;
}

/* 总线匹配：优先使用总线的自定义 match，否则退化为按名称匹配。 */
static int bus_match(struct bus_type *bus, struct device *dev, struct driver *drv)
{
    if (bus->match) {
        return bus->match(dev, drv);
    }

    return default_name_match(dev, drv);
}

/**
 * 单设备扫描绑定：遍历 bus->drivers 链表，找到第一个匹配的驱动后调用 probe()。
 * 若设备已有驱动绑定或 probe 失败则不再继续。
 * 使用 container_of 从链表节点还原驱动指针。
 */
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

/* 创建总线：分配 bus_type 结构体并初始化 devices/drivers 双向链表。 */
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

/* 销毁总线，释放内存。调用者需确保 devices/drivers 已清空。 */
void bus_destroy(struct bus_type *bus)
{
    if (!bus) {
        return;
    }

    pr_info("bus: %s destroyed", bus->name ? bus->name : "<unnamed>");
    kfree(bus);
}

/* 注册驱动：将驱动加入总线 drivers 链表。不立即 probe（等设备注册时触发）。 */
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

    return 0;
}

/* 注销驱动：调用 remove() 解绑所有关联设备，从 drivers 链表移除。 */
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

/* 注册设备：加入总线 devices 链表并立即触发 probe 尝试绑定驱动。 */
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

/* 注销设备：调用驱动的 remove() 解绑，从 devices 链表移除。 */
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

/* 单设备 probe：尝试为该设备匹配并绑定驱动，返回 0 表示成功。 */
int bus_probe(struct bus_type *bus, struct device *dev)
{
    if (!bus || !dev) {
        return -1;
    }

    bus_probe_device(bus, dev);
    return dev->drv ? 0 : -1;
}

/* 批量 probe：遍历所有未绑定驱动的设备，逐一尝试绑定。 */
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

/* 重新 probe 指定设备：适用于驱动依赖就绪后重新尝试绑定的场景。 */
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

/* 检查指定名称的设备是否已注册。 */
int bus_device_exists(struct bus_type *bus, const char *name)
{
    return bus_find_device(bus, name) ? 1 : 0;
}

/* 按名称在总线 devices 链表中查找设备（O(n) 线性扫描）。 */
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
