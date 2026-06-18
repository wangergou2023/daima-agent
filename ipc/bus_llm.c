/* LLM 总线注册：协议驱动 + 模型设备 */
#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "cjson.h"
#include "net/http.h"

static int llm_device_health_check(struct device *dev)
{
    if (!dev->data)
        return 0;  /* 无 data 字段，跳过检查，直接绑定 */

    cJSON *data = cJSON_Parse((const char *)dev->data);
    if (!data)
        return 0;

    const char *health_url = cJSON_GetStringValue(
        cJSON_GetObjectItem(data, "health_url"));
    if (!health_url || !health_url[0]) {
        cJSON_Delete(data);
        return 0;
    }

    host_http_response_t resp = {0};
    err_t err = host_http_request("GET", health_url, NULL, NULL, 5000, &resp);
    if (err != 0 || resp.status < 200 || resp.status >= 400) {
        pr_warn("llm: %s health check FAILED url=%s err=%s status=%ld",
                dev->name, health_url, err_name(err), resp.status);
        host_http_response_free(&resp);
        cJSON_Delete(data);
        return -1;
    }

    pr_info("llm: %s health check OK url=%s status=%ld",
            dev->name, health_url, resp.status);
    host_http_response_free(&resp);
    cJSON_Delete(data);
    return 0;
}

static int openai_compatible_probe(struct device *dev)
{
    return llm_device_health_check(dev);
}

static int anthropic_compatible_probe(struct device *dev)
{
    return llm_device_health_check(dev);
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
