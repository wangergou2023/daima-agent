/* of_populate: JSON 设备树解析 — 统一 device 注册入口 */
#include "linux/bus.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "cjson.h"
#include "paths.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static struct bus_type *find_bus(const char *name)
{
    if (!name) return NULL;
    /* 3 条总线，按名称查找 */
    if (tool_bus && strcmp(tool_bus->name, name) == 0) return tool_bus;
    if (channel_bus && strcmp(channel_bus->name, name) == 0) return channel_bus;
    if (llm_bus && strcmp(llm_bus->name, name) == 0) return llm_bus;
    return NULL;
}

int of_populate(const char *json_path)
{
    if (!json_path) return -1;

    FILE *f = fopen(json_path, "r");
    if (!f) {
        pr_debug("of: %s not found", json_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0 || len > 1048576) { fclose(f); return -1; }
    rewind(f);

    char *buf = kmalloc(len + 1, GFP_KERNEL);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, len, f);
    fclose(f);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    if (!root) return -1;

    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    int count = 0;
    if (devices && cJSON_IsArray(devices)) {
        cJSON *item;
        cJSON_ArrayForEach(item, devices) {
            const char *bus_name = cJSON_GetStringValue(
                cJSON_GetObjectItem(item, "bus"));
            const char *dev_name = cJSON_GetStringValue(
                cJSON_GetObjectItem(item, "name"));

            if (!bus_name || !dev_name) continue;

            struct bus_type *bus = find_bus(bus_name);
            if (!bus) {
                pr_warn("of: bus '%s' not found for device '%s'",
                        bus_name, dev_name);
                continue;
            }

            if (bus_device_exists(bus, dev_name))
                continue;

            struct device *dev = kmalloc(sizeof(*dev), GFP_KERNEL);
            if (!dev) continue;
            memset(dev, 0, sizeof(*dev));

            /* strdup name: cJSON 内部字符串会在 cJSON_Delete(root) 后失效 */
            dev->name = strdup(dev_name);
            if (!dev->name) { kfree(dev); continue; }

            /* 可选的 data 字段：序列化为 JSON 字符串存入 dev->data */
            cJSON *data_obj = cJSON_GetObjectItem(item, "data");
            if (data_obj) {
                char *data_str = cJSON_PrintUnformatted(data_obj);
                if (data_str)
                    dev->data = data_str;
            }

            device_register(dev, bus);
            count++;
        }
    }
    cJSON_Delete(root);

    if (count > 0)
        pr_info("of: populated %d devices from %s", count, json_path);
    return count;
}

int of_populate_default(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/device_tree.json", path_spiffs_base());
    return of_populate(path);
}
