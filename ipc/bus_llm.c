/* LLM 总线注册：协议驱动 + 模型设备 */
#include "linux/bus.h"
#include "linux/printk.h"
#include "drivers/llm/llm_proxy.h"

static int openai_compatible_probe(struct device *dev)
{
    (void)dev;
    pr_info("llm: probing openai_compatible");
    return 0;
}

static int anthropic_compatible_probe(struct device *dev)
{
    (void)dev;
    pr_info("llm: probing anthropic_compatible");
    return 0;
}

static struct driver openai_compatible_drv = {
    .name = "openai_compatible",
    .probe = openai_compatible_probe,
};

static struct driver anthropic_compatible_drv = {
    .name = "anthropic_compatible",
    .probe = anthropic_compatible_probe,
};

int bus_llm_register_all(void)
{
    if (!llm_bus) {
        pr_err("llm_bus not initialized");
        return -1;
    }

    driver_register(&openai_compatible_drv, llm_bus);
    driver_register(&anthropic_compatible_drv, llm_bus);

    return 0;
}
