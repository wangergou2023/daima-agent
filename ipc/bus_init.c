/* 总线子系统初始化：创建三条总线实例 */
#include "linux/bus.h"
#include "linux/printk.h"

/* 三条总线全局实例 */
struct bus_type *tool_bus;
struct bus_type *channel_bus;
struct bus_type *llm_bus;

static int tool_bus_match(struct device *dev, struct driver *drv)
{
    (void)dev;
    (void)drv;
    return 0;
}

int bus_init(void)
{
    pr_info("Initializing bus subsystem...");

    tool_bus = bus_create("tool_bus", tool_bus_match);
    if (!tool_bus) return -1;

    channel_bus = bus_create("channel_bus", NULL);
    if (!channel_bus) return -1;

    llm_bus = bus_create("llm_bus", NULL);
    if (!llm_bus) return -1;

    pr_info("Bus subsystem ready (3 buses)");
    return 0;
}