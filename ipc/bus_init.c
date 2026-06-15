/* 总线子系统初始化：创建四条总线实例 */
#include "linux/bus.h"
#include "linux/printk.h"

/* 四条总线全局实例 */
struct bus_type *tool_bus;
struct bus_type *mcp_bus;
struct bus_type *channel_bus;
struct bus_type *llm_bus;

int bus_init(void)
{
    pr_info("Initializing bus subsystem...");

    /* tool_bus: Agent 可调用的工具。字符串精确匹配 */
    tool_bus = bus_create("tool_bus", NULL);
    if (!tool_bus) return -1;

    /* mcp_bus: 底层执行能力（不直接暴露给 Agent） */
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
