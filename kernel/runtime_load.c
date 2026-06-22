/* 运行时配置加载。 */

#include "runtime.h"
#include "paths.h"
#include "runtime_defaults.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <unistd.h>

#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"

static char *read_config_text(void)
{
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());
    FILE *f = fopen(cfg_path, "rb");
    char *buf = NULL;
    long size = 0;
    size_t n = 0;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || size > 128 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

err_t runtime_config_init(void)
{
    char *text = NULL;
    cJSON *root = NULL;
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());

    runtime_config_reset_defaults(&s_cfg);

    if (access(cfg_path, F_OK) != 0) {
        pr_warn("Runtime config missing: %s", cfg_path);
        pr_warn("Please create it with reference to: %s/config.example.json", path_config_dir());
        s_cfg.loaded = 1;
        return 0;
    }

    text = read_config_text();
    if (!text) {
        pr_warn("Cannot read runtime config: %s", cfg_path);
        s_cfg.loaded = 1;
        return 0;
    }

    root = cJSON_Parse(text);
    kfree(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        pr_warn("Invalid runtime config JSON: %s", cfg_path);
        s_cfg.loaded = 1;
        return 0;
    }

    runtime_config_apply_values(&s_cfg, root);
    cJSON_Delete(root);
    s_cfg.loaded = 1;

    pr_info("Runtime config loaded: %s%s%s", cfg_path, s_cfg.active_provider[0] ? " active_provider=" : "", s_cfg.active_provider[0] ? s_cfg.active_provider : "");
    return 0;
}
