/* 总线模型核心接口：bus_type/device/driver 三类总线的注册与探测。 */

#pragma once

#include "list.h"
#include "driver.h"
#include <stddef.h>

/* 三根系统总线指针（由各自子系统初始化时赋值） */
extern struct bus_type *tool_bus;      /* 工具总线 */
extern struct bus_type *channel_bus;   /* 通道总线 */
extern struct bus_type *llm_bus;       /* 大模型总线 */

/* 总线类型：管理一类驱动与设备的集合 */
struct bus_type {
	const char *name;                      /* 总线名称（如 "tool"/"channel"/"llm"） */
	int (*match)(struct device *dev,
		     struct driver *drv);      /* 设备与驱动的匹配回调 */
	struct list_head devices;              /* 挂载在该总线上的设备链表 */
	struct list_head drivers;              /* 注册到该总线的驱动链表 */
};

/* 设备依赖声明（用于延迟探测） */
struct dependency {
	const char *bus_name;  /* 依赖的总线名称 */
	const char *dev_name;  /* 依赖的设备名称 */
};

/* 总线设备：挂载在某个总线上的设备实例 */
struct device {
	const char *name;                   /* 设备名称 */
	struct bus_type *bus;              /* 所属总线指针 */
	struct dependency *dependencies;   /* 依赖列表（可为 NULL） */
	int dep_count;                      /* 依赖数量 */
	void *data;                         /* 设备私有数据 */
	struct driver *drv;               /* 绑定到的驱动（probe 成功后填充） */
	struct list_head bus_node;         /* 在总线设备链表中的节点 */
};

/**
 * 创建一条新总线。
 * @param name   总线名称
 * @param match  设备与驱动的匹配回调（NULL 则使用默认 strcmp 匹配）
 * @return 成功返回总线指针，失败返回 NULL
 */
struct bus_type *bus_create(const char *name,
			    int (*match)(struct device *dev, struct driver *drv));
/**
 * 销毁一条总线，释放所有关联的设备与驱动节点。
 * @param bus  总线指针
 */
void bus_destroy(struct bus_type *bus);

/**
 * 将设备注册到指定总线。若所有依赖已满足则自动 probe。
 * @param dev  设备指针
 * @param bus  目标总线指针
 * @return 0 成功，负数错误码
 */
int device_register(struct device *dev, struct bus_type *bus);
/**
 * 从总线注销设备。
 * @param dev  设备指针
 */
void device_unregister(struct device *dev);

/**
 * 为指定设备遍历总线上的驱动并调用 match/probe。
 * @param bus  总线指针
 * @param dev  设备指针
 * @return 0 成功绑定，负数错误码
 */
int bus_probe(struct bus_type *bus, struct device *dev);
/**
 * 对总线上所有未绑定设备执行 probe。
 * @param bus  总线指针
 */
void bus_probe_all(struct bus_type *bus);
/**
 * 重新探测指定名称的设备（用于依赖满足后重试）。
 * @param bus      总线指针
 * @param dev_name 设备名称
 */
void bus_reprobe(struct bus_type *bus, const char *dev_name);

/**
 * 检查指定名称的设备是否已在总线上。
 * @param bus   总线指针
 * @param name  设备名称
 * @return 1 存在，0 不存在
 */
int bus_device_exists(struct bus_type *bus, const char *name);
/**
 * 按名称查找总线上的设备。
 * @param bus   总线指针
 * @param name  设备名称
 * @return 找到返回设备指针，未找到返回 NULL
 */
struct device *bus_find_device(struct bus_type *bus, const char *name);

/**
 * 注册所有通道驱动到 channel_bus。
 * @return 0 成功，负数错误码
 */
int bus_channel_register_all(void);
/**
 * 注册所有大模型驱动到 llm_bus。
 * @return 0 成功，负数错误码
 */
int bus_llm_register_all(void);
