/* 自定义工具：从 JSON 加载零编译 tool，通过已有 driver 执行 */
#include "drivers/tool/tool_types.h"
#include "drivers/tool/tool_custom.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include "linux/bus.h"
#include "paths.h"

#define MAX_CUSTOM_TOOLS 16
#define CUSTOM_NAME_LEN 64

typedef struct {
    char name[CUSTOM_NAME_LEN];
    char description[256];
    char base_driver[64];
    char command[4096];
    struct tool_device dev;
    struct tool_driver drv;
    struct device *bus_dev;
    bool active;
} custom_tool_t;

static custom_tool_t s_tools[MAX_CUSTOM_TOOLS];
static int s_count;

static int custom_probe(struct device *dev) { (void)dev; return 0; }

static err_t custom_execute_stub(const char *in, char *out, size_t sz)
{ (void)in; snprintf(out, sz, "custom tool"); return 0; }

static custom_tool_t *find(const char *name)
{
    for (int i = 0; i < s_count; i++)
        if (s_tools[i].active && strcmp(s_tools[i].name, name) == 0)
            return &s_tools[i];
    return NULL;
}

err_t tool_custom_execute(const char *name, const char *input,
                           char *output, size_t size)
{
    custom_tool_t *ct = find(name);
    if (!ct) return ERR_NOT_FOUND;

    struct device *bd = bus_find_device(tool_bus, ct->base_driver);
    if (!bd || !bd->drv) {
        snprintf(output, size, "base driver '%s' not available", ct->base_driver);
        return ERR_NOT_FOUND;
    }
    struct tool_driver *td = container_of(bd->drv, struct tool_driver, drv);
    if (!td->execute) return ERR_FAIL;

    if (ct->command[0]) {
        char wrap[4608];
        if (input && input[0] && strcmp(input, "{}") != 0)
            snprintf(wrap, sizeof(wrap), "{\"command\":\"%s\",\"input\":%s}",
                     ct->command, input);
        else
            snprintf(wrap, sizeof(wrap), "{\"command\":\"%s\"}", ct->command);
        return td->execute(wrap, output, size);
    }
    return td->execute(input, output, size);
}

int tool_custom_count(void)
{
    return s_count;
}

const struct tool_custom_meta *tool_custom_get(int index)
{
    static struct tool_custom_meta meta;

    if (index < 0 || index >= s_count || !s_tools[index].active) {
        return NULL;
    }

    meta.name = s_tools[index].name;
    meta.description = s_tools[index].description;
    meta.input_schema_json = s_tools[index].dev.input_schema_json;
    return &meta;
}

static int load_one(cJSON *item)
{
    if (s_count >= MAX_CUSTOM_TOOLS) return -1;
    const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
    const char *d = cJSON_GetStringValue(cJSON_GetObjectItem(item, "driver"));
    if (!n || !n[0] || !d || !d[0]) return -1;
    if (!bus_device_exists(tool_bus, d)) return -1;

    custom_tool_t *ct = &s_tools[s_count];
    memset(ct, 0, sizeof(*ct));
    strscpy(ct->name, n, sizeof(ct->name));
    strscpy(ct->base_driver, d, sizeof(ct->base_driver));
    const char *desc = cJSON_GetStringValue(cJSON_GetObjectItem(item, "description"));
    const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(item, "command"));
    const char *schema = cJSON_GetStringValue(cJSON_GetObjectItem(item, "input_schema_json"));
    if (desc) strscpy(ct->description, desc, sizeof(ct->description));
    if (cmd) strscpy(ct->command, cmd, sizeof(ct->command));

    ct->dev.name = ct->name;
    ct->dev.description = ct->description;
    ct->dev.input_schema_json = schema ? schema : "{\"type\":\"object\"}";

    ct->drv.drv.name = ct->name;
    ct->drv.drv.probe = custom_probe;
    ct->drv.execute = custom_execute_stub;
    INIT_LIST_HEAD(&ct->drv.drv.node);

    ct->bus_dev = kmalloc(sizeof(*ct->bus_dev), GFP_KERNEL);
    if (!ct->bus_dev) return -1;
    memset(ct->bus_dev, 0, sizeof(*ct->bus_dev));
    ct->bus_dev->name = ct->dev.name;
    ct->bus_dev->data = &ct->dev;

    driver_register(&ct->drv.drv, tool_bus);
    device_register(ct->bus_dev, tool_bus);
    ct->active = true;
    s_count++;
    pr_info("custom_tool: %s → %s", n, d);
    return 0;
}

int tool_custom_load(const char *path)
{
    if (!path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0 || len > 1048576) { fclose(f); return -1; }
    rewind(f);
    char *buf = kmalloc(len + 1, GFP_KERNEL);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, len, f); fclose(f); buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf); kfree(buf);
    if (!root) return -1;
    cJSON *tools = cJSON_GetObjectItem(root, "tools");
    int n = 0;
    if (tools && cJSON_IsArray(tools)) {
        cJSON *item;
        cJSON_ArrayForEach(item, tools)
            if (load_one(item) == 0) n++;
    }
    cJSON_Delete(root);
    if (n) pr_info("custom_tool: %d loaded from %s", n, path);
    return n;
}

int tool_custom_load_default(void)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/custom_tools.json", path_spiffs_base());
    return tool_custom_load(p);
}
