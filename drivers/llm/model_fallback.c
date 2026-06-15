#include "drivers/llm/model_fallback.h"

#include "paths.h"
#include "runtime.h"
#include "autoconf.h"
#include "linux/printk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"
#include "linux/kernel.h"
static bool add_model(model_fallback_cfg_t *cfg, const char *model);

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    strscpy(dst, src ? src : "", dst_size);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || len > 128 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)kzalloc((size_t)len + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static void load_default_cfg(model_fallback_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    const char *active_model = runtime_config_get_provider_model();
    if (!active_model || !active_model[0]) {
        pr_warn("No active provider model configured, fallback disabled");
        cfg->enabled = false;
        return;
    }

    cfg->enabled = true;
    safe_copy(cfg->models[0], sizeof(cfg->models[0]), active_model);
    cfg->model_count = 1;

    pr_debug("Model fallback default: %s (from active provider)", active_model);
}

static bool add_model(model_fallback_cfg_t *cfg, const char *model)
{
    if (!cfg || !model || !model[0] || cfg->model_count >= FALLBACK_MAX_MODELS) {
        return false;
    }
    for (int i = 0; i < cfg->model_count; i++) {
        if (strcmp(cfg->models[i], model) == 0) {
            return false;
        }
    }
    safe_copy(cfg->models[cfg->model_count], sizeof(cfg->models[cfg->model_count]), model);
    cfg->model_count++;
    return true;
}

static bool parse_models_array(model_fallback_cfg_t *cfg, cJSON *models)
{
    if (!cJSON_IsArray(models)) {
        return false;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = true;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, models) {
        if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
            add_model(cfg, item->valuestring);
        }
    }
    return cfg->model_count > 0;
}

static bool load_json_cfg(model_fallback_cfg_t *cfg, const char *json_text)
{
    cJSON *json_root = cJSON_Parse(json_text);
    if (!json_root || !cJSON_IsObject(json_root)) {
        cJSON_Delete(json_root);
        return false;
    }

    cJSON *root = json_root;
    cJSON *fallback_root = cJSON_GetObjectItem(root, "model_fallback");
    if (cJSON_IsObject(fallback_root)) {
        root = fallback_root;
    }

    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    cJSON *models = cJSON_GetObjectItem(root, "fallback_models");
    if (!models) {
        models = cJSON_GetObjectItem(root, "models");
    }

    bool ok = parse_models_array(cfg, models);
    if (!ok && root == json_root) {
        cJSON *active_provider = cJSON_GetObjectItem(root, "active_provider");
        cJSON *providers = cJSON_GetObjectItem(root, "providers");
        if (cJSON_IsString(active_provider) && active_provider->valuestring &&
            cJSON_IsObject(providers)) {
            memset(cfg, 0, sizeof(*cfg));
            cfg->enabled = true;
            cJSON *provider = NULL;
            cJSON_ArrayForEach(provider, providers) {
                if (!provider->string || strcmp(provider->string, active_provider->valuestring) == 0) {
                    continue;
                }
                cJSON *model = cJSON_GetObjectItem(provider, "model");
                if (cJSON_IsString(model) && model->valuestring && model->valuestring[0]) {
                    add_model(cfg, model->valuestring);
                }
            }
            ok = cfg->model_count > 0;
        }
    }
    if (ok && cJSON_IsBool(enabled)) {
        cfg->enabled = cJSON_IsTrue(enabled);
    }
    cJSON_Delete(json_root);
    return ok;
}

static const char *config_dir_for_load(char *env_config_dir, size_t env_config_dir_size)
{
    const char *env_home = getenv("DAIMA_HOME");
    if (env_home && env_home[0]) {
        snprintf(env_config_dir, env_config_dir_size, "%s/spiffs_data/config", env_home);
        return env_config_dir;
    }
    return path_config_dir();
}

static char *read_fallback_config(char *out_path, size_t out_path_size)
{
    char env_config_dir[BUF_PATH];
    const char *config_dir = config_dir_for_load(env_config_dir, sizeof(env_config_dir));

    snprintf(out_path, out_path_size, "%s/category_routing.json", config_dir);
    char *json_text = read_file(out_path);
    if (json_text) {
        return json_text;
    }

    snprintf(out_path, out_path_size, "%s/fallback_models.json", config_dir);
    json_text = read_file(out_path);
    if (json_text) {
        return json_text;
    }

    snprintf(out_path, out_path_size, "%s/config.json", config_dir);
    return read_file(out_path);
}

model_fallback_cfg_t model_fallback_load_cfg(void)
{
    model_fallback_cfg_t cfg;
    const char *env_enabled = getenv("MODEL_FALLBACK_ENABLED");
    if (env_enabled && strcmp(env_enabled, "0") == 0) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.enabled = false;
        return cfg;
    }

    char path[BUF_PATH];
    char *json_text = read_fallback_config(path, sizeof(path));
    if (json_text) {
        if (!load_json_cfg(&cfg, json_text)) {
            pr_warn("Invalid model fallback config, using defaults: %s", path);
            load_default_cfg(&cfg);
        }
        kfree(json_text);
        return cfg;
    }

    load_default_cfg(&cfg);
    return cfg;
}

err_t model_fallback_chat_with_fallback(const char *system_prompt,
                                              cJSON *messages,
                                              const char *tools_json,
                                              llm_response_t *resp)
{
    if (!resp) {
        return ERR_INVALID_ARG;
    }

    char primary_model[64];
    safe_copy(primary_model, sizeof(primary_model), llm_get_model_name());

    err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
    if (err == 0) {
        llm_set_model(primary_model);
        return 0;
    }

    model_fallback_cfg_t cfg = model_fallback_load_cfg();
    if (!cfg.enabled || cfg.model_count <= 0) {
        llm_set_model(primary_model);
        return err;
    }

    err_t last_err = err;
    for (int i = 0; i < cfg.model_count; i++) {
        if (strcmp(cfg.models[i], primary_model) == 0) {
            continue;
        }

        llm_set_model(cfg.models[i]);
        last_err = llm_chat_tools(system_prompt, messages, tools_json, resp);
        if (last_err == 0) {
            pr_info("Model fallback: primary失败 -> %s", cfg.models[i]);
            llm_set_model(primary_model);
            return 0;
        }
    }

    llm_set_model(primary_model);
    return last_err;
}
