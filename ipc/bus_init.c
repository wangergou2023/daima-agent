/* 总线子系统初始化：创建四条总线实例 */
#include "linux/bus.h"
#include "linux/printk.h"

/* 四条总线全局实例 */
struct bus_type *tool_bus;
struct bus_type *mcp_bus;
struct bus_type *channel_bus;
struct bus_type *llm_bus;

/*
 * tool_bus_match - catch-all 匹配
 * tool_bus 上所有 device 由同一个 generic driver 处理，
 * 不做名称匹配，任何 device 匹配任何 driver。
 */
static int tool_bus_match(struct device *dev, struct driver *drv)
{
    (void)dev;
    (void)drv;
    return 0;
}

int bus_init(void)
{
    pr_info("Initializing bus subsystem...");

    /* tool_bus: Agent 可调用的工具。catch-all match（所有工具共用一个 driver） */
    tool_bus = bus_create("tool_bus", tool_bus_match);
    if (!tool_bus) return -1;

    /* mcp_bus: 底层执行能力（不直接暴露给 Agent）。字符串精确匹配 */
    mcp_bus = bus_create("mcp_bus", NULL);
    if (!mcp_bus) return -1;

    /* channel_bus: 消息通道 */
    channel_bus = bus_create("channel_bus", NULL);
    if (!channel_bus) return -1;

    /* llm_bus: LLM 后端 */
    llm_bus = bus_create("llm_bus", NULL);
    if (!llm_bus) return -1;

    pr_info("Bus subsystem ready (4 buses)");
    return 0;
}
