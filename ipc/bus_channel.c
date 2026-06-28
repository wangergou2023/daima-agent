/* 通道总线注册：四条通道驱动 + 设备 */
#include "linux/bus.h"
#include "linux/printk.h"
#include "drivers/channel/feishu/feishu_bot.h"
#include "drivers/channel/vector/vector_channel.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/voice/voice_channel.h"

static int feishu_channel_probe(struct device *dev)
{
    (void)dev;
    pr_info("channel: probing feishu");
    if (feishu_bot_init() != 0) return -1;
    return feishu_bot_start();
}

static int vector_channel_probe(struct device *dev)
{
    (void)dev;
    pr_info("channel: probing vector");
    return vector_channel_init();
}

static int voice_channel_probe(struct device *dev)
{
    (void)dev;
    pr_info("channel: probing voice");
    return voice_channel_init();
}

static int gateway_channel_probe(struct device *dev)
{
    (void)dev;
    pr_info("channel: probing gateway");
    return 0;
}

static struct driver feishu_channel_drv = {
    .name = "feishu",
    .probe = feishu_channel_probe,
};

static struct driver vector_channel_drv = {
    .name = "vector",
    .probe = vector_channel_probe,
};

static struct driver voice_channel_drv = {
    .name = "voice",
    .probe = voice_channel_probe,
};

static struct driver gateway_channel_drv = {
    .name = "gateway",
    .probe = gateway_channel_probe,
};

int bus_channel_register_all(void)
{
    if (!channel_bus) {
        pr_err("channel_bus not initialized");
        return -1;
    }

    driver_register(&feishu_channel_drv, channel_bus);
    driver_register(&vector_channel_drv, channel_bus);
    driver_register(&voice_channel_drv, channel_bus);
    driver_register(&gateway_channel_drv, channel_bus);

    /* 注册设备 */
    struct device *devs[] = {
        &(struct device){.name = "feishu"},
        &(struct device){.name = "vector"},
        &(struct device){.name = "voice"},
        &(struct device){.name = "gateway"},
    };
    for (size_t i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        device_register(devs[i], channel_bus);
    }

    return 0;
}
