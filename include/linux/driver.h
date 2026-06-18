/* 驱动模型核心接口：驱动注册、probe/remove 生命周期。 */

#pragma once

#include "init.h"
#include "list.h"

struct bus_type;
struct device;

/* 驱动：实现 probe/remove 回调并注册到总线 */
struct driver {
	const char *name;                  /* 驱动名称 */
	struct bus_type *bus;             /* 所属总线指针 */
	int (*probe)(struct device *dev); /* 设备探测回调（初始化资源） */
	void (*remove)(struct device *dev); /* 设备移除回调（释放资源） */
	void *priv;                        /* 驱动私有数据 */
	struct list_head node;            /* 在总线驱动链表中的节点 */
};

/**
 * 将驱动注册到指定总线。
 * @param drv  驱动指针
 * @param bus  目标总线指针
 * @return 0 成功，负数错误码
 */
int driver_register(struct driver *drv, struct bus_type *bus);
/**
 * 从总线注销驱动。
 * @param drv  驱动指针
 */
void driver_unregister(struct driver *drv);
