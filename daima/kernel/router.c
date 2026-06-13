#include "router.h"

#include "paths.h"
#include "runtime.h"
#include "cJSON.h"
#include "autoconf.h"
#include "linux/printk.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"

static const char *TAG = "category_router";

static category_router_cfg_t s_cfg;
static bool s_loaded = false;
static char s_loaded_home[DAIMA_BUF_PATH];

typedef struct {
    char provider_name[64];
    char model[64];
    int context_limit;
    int max_tokens;
} category_model_resolution_t;

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void init_empty_cfg(category_router_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = true;
    for (int i = 0; i < DAIMA_INTENT_COUNT; i++) {
        cfg->intent_map[i] = -1;
    }
    for (int i = 0; i < AGENT_ROLE_COUNT; i++) {
        cfg->role_model_map[i] = -1;
    }
}

static int add_profile(category_router_cfg_t *cfg,
                       const char *name,
                       const char *model,
                       int context_limit,
                       int max_tokens)
{
    if (!cfg || !name || !model || cfg->profile_count >= CATEGORY_ROUTER_MAX_PROFILES) {
        return -1;
    }

    int idx = cfg->profile_count++;
    daima_category_profile_t *profile = &cfg->profiles[idx];
    safe_copy(profile->name, sizeof(profile->name), name);
    safe_copy(profile->model, sizeof(profile->model), model);
    profile->context_limit = context_limit;
    profile->max_tokens = max_tokens;
    return idx;
}

static int find_profile_index(const category_router_cfg_t *cfg, const char *name)
{
    if (!cfg || !name) {
        return -1;
    }
    for (int i = 0; i < cfg->profile_count; i++) {
        if (strcmp(cfg->profiles[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static daima_intent_t intent_from_key(const char *key)
{
    if (!key) {
        return DAIMA_INTENT_COUNT;
    }
    if (strcmp(key, "qa") == 0) return DAIMA_INTENT_QA;
    if (strcmp(key, "implement") == 0) return DAIMA_INTENT_IMPLEMENT;
    if (strcmp(key, "investigate") == 0) return DAIMA_INTENT_INVESTIGATE;
    if (strcmp(key, "fix") == 0) return DAIMA_INTENT_FIX;
    if (strcmp(key, "open") == 0) return DAIMA_INTENT_OPEN;
    return DAIMA_INTENT_COUNT;
}

static int role_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < AGENT_ROLE_COUNT; i++) {
        if (strcmp(name, agent_role_name((agent_role_t)i)) == 0) return i;
    }
    return -1;
}

static void set_default_intent_map(category_router_cfg_t *cfg, int deep, int quick)
{
    if (!cfg) {
        return;
    }
    cfg->intent_map[DAIMA_INTENT_QA] = quick;
    cfg->intent_map[DAIMA_INTENT_IMPLEMENT] = deep;
    cfg->intent_map[DAIMA_INTENT_INVESTIGATE] = deep;
    cfg->intent_map[DAIMA_INTENT_FIX] = deep;
    cfg->intent_map[DAIMA_INTENT_OPEN] = quick;
}

static void load_default_cfg(category_router_cfg_t *cfg)
{
    init_empty_cfg(cfg);

    const char *active_model = runtime_config_get_provider_model();
    if (!active_model || !active_model[0]) {
        DAIMA_LOGW(TAG, "No active provider model configured, category routing disabled");
        cfg->enabled = false;
        return;
    }

    int context_limit = runtime_config_get_context_limit_tokens();
    int max_tokens = runtime_config_get_max_output_tokens();

    const char *quick_model = runtime_config_get_provider_model_for_name("ingenic_local_kimi");
    if (!quick_model || !quick_model[0]) {
        quick_model = active_model;
    }

    int deep = add_profile(cfg, "deep", active_model, context_limit, max_tokens);
    int quick = add_profile(cfg, "quick", quick_model, context_limit, max_tokens);

    set_default_intent_map(cfg, deep, quick);

    cfg->role_model_map[AGENT_ROLE_FAST] = quick;
    cfg->role_model_map[AGENT_ROLE_PLANNER] = deep;
    cfg->role_model_map[AGENT_ROLE_EXECUTOR] = deep;
    cfg->role_model_map[AGENT_ROLE_REVIEWER] = deep;

    DAIMA_LOGI(TAG, "Category routing defaults: deep=%s quick=%s",
                active_model, quick_model);
}

static void add_profiles_from_provider_object(category_router_cfg_t *cfg, cJSON *profiles)
{
    cJSON *entry = NULL;
    int context_limit = runtime_config_get_context_limit_tokens();
    int max_tokens = runtime_config_get_max_output_tokens();

    cJSON_ArrayForEach(entry, profiles) {
        if (!entry->string || !entry->string[0] || !cJSON_IsString(entry)) {
            continue;
        }
        const char *model = runtime_config_get_provider_model_for_name(entry->valuestring);
        if (!model || !model[0]) {
            DAIMA_LOGW(TAG, "Category routing provider not found or missing model: %s", entry->valuestring);
            continue;
        }
        add_profile(cfg, entry->string, model, context_limit, max_tokens);
    }
}

static bool read_provider_limits(cJSON *provider, int *context_limit, int *max_tokens)
{
    if (!cJSON_IsObject(provider)) {
        return false;
    }
    cJSON *context = cJSON_GetObjectItem(provider, "context_limit_tokens");
    cJSON *max_out = cJSON_GetObjectItem(provider, "max_output_tokens");
    if (context_limit) {
        *context_limit = cJSON_IsNumber(context) ? context->valueint : runtime_config_get_context_limit_tokens();
    }
    if (max_tokens) {
        *max_tokens = cJSON_IsNumber(max_out) ? max_out->valueint : runtime_config_get_max_output_tokens();
    }
    return true;
}

static bool find_provider_by_model(cJSON *json_root,
                                   const char *model,
                                   category_model_resolution_t *out)
{
    if (!json_root || !model || !model[0] || !out) {
        return false;
    }

    cJSON *providers = cJSON_GetObjectItem(json_root, "providers");
    if (!cJSON_IsObject(providers)) {
        return false;
    }

    cJSON *provider = NULL;
    cJSON_ArrayForEach(provider, providers) {
        cJSON *provider_model = cJSON_GetObjectItem(provider, "model");
        if (!provider->string || !cJSON_IsString(provider_model) ||
            strcmp(provider_model->valuestring, model) != 0) {
            continue;
        }

        safe_copy(out->provider_name, sizeof(out->provider_name), provider->string);
        safe_copy(out->model, sizeof(out->model), provider_model->valuestring);
        read_provider_limits(provider, &out->context_limit, &out->max_tokens);
        return true;
    }

    return false;
}

static bool find_provider_by_name(cJSON *json_root,
                                   const char *provider_name,
                                   category_model_resolution_t *out)
{
    if (!json_root || !provider_name || !provider_name[0] || !out) {
        return false;
    }

    cJSON *providers = cJSON_GetObjectItem(json_root, "providers");
    if (!cJSON_IsObject(providers)) {
        return false;
    }

    cJSON *provider = cJSON_GetObjectItem(providers, provider_name);
    if (!cJSON_IsObject(provider)) {
        return false;
    }

    cJSON *p_model = cJSON_GetObjectItem(provider, "model");
    if (!cJSON_IsString(p_model)) {
        return false;
    }

    safe_copy(out->provider_name, sizeof(out->provider_name), provider_name);
    safe_copy(out->model, sizeof(out->model), p_model->valuestring);
    read_provider_limits(provider, &out->context_limit, &out->max_tokens);
    return true;
}

static bool resolve_active_provider_model(cJSON *json_root, category_model_resolution_t *out)
{
    if (!out) {
        return false;
    }

    const char *active_model = runtime_config_get_provider_model();
    if (!active_model || !active_model[0]) {
        return false;
    }

    if (find_provider_by_model(json_root, active_model, out)) {
        return true;
    }

    safe_copy(out->provider_name, sizeof(out->provider_name), runtime_config_get_active_provider_name());
    safe_copy(out->model, sizeof(out->model), active_model);
    out->context_limit = runtime_config_get_context_limit_tokens();
    out->max_tokens = runtime_config_get_max_output_tokens();
    return true;
}

static bool resolve_category_model(cJSON *json_root,
                                   cJSON *category,
                                   category_model_resolution_t *out)
{
    if (!cJSON_IsObject(category) || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    cJSON *provider_prefs = cJSON_GetObjectItem(category, "provider_preference");
    if (cJSON_IsArray(provider_prefs)) {
        cJSON *pref = NULL;
        cJSON_ArrayForEach(pref, provider_prefs) {
            if (!cJSON_IsString(pref)) continue;
            if (find_provider_by_name(json_root, pref->valuestring, out)) {
                return true;
            }
        }
    }

    cJSON *preferences = cJSON_GetObjectItem(category, "model_preference");
    if (cJSON_IsArray(preferences)) {
        cJSON *preference = NULL;
        cJSON_ArrayForEach(preference, preferences) {
            if (!cJSON_IsString(preference)) {
                continue;
            }
            if (find_provider_by_model(json_root, preference->valuestring, out)) {
                return true;
            }
        }
    }

    return resolve_active_provider_model(json_root, out);
}

static void parse_categories_json(category_router_cfg_t *cfg, cJSON *json_root, cJSON *categories)
{
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, categories) {
        if (!entry->string || !entry->string[0]) {
            continue;
        }

        category_model_resolution_t resolved;

        if (cJSON_IsString(entry)) {
            if (!find_provider_by_name(json_root, entry->valuestring, &resolved)) {
                DAIMA_LOGW(TAG, "Category routing provider not found: %s → %s",
                           entry->string, entry->valuestring);
                continue;
            }
        } else if (cJSON_IsObject(entry)) {
            if (!resolve_category_model(json_root, entry, &resolved)) {
                DAIMA_LOGW(TAG, "Category routing category has no available model: %s", entry->string);
                continue;
            }
        } else {
            continue;
        }

        add_profile(cfg, entry->string, resolved.model, resolved.context_limit, resolved.max_tokens);
    }
}

static void log_loaded_categories(const category_router_cfg_t *cfg, cJSON *json_root)
{
    if (!cfg || cfg->profile_count <= 0) {
        return;
    }

    DAIMA_LOGI(TAG, "Category routing: categories loaded");
    for (int i = 0; i < cfg->profile_count; i++) {
        category_model_resolution_t resolved;
        const char *provider_name = "active_provider";
        if (find_provider_by_model(json_root, cfg->profiles[i].model, &resolved) && resolved.provider_name[0]) {
            provider_name = resolved.provider_name;
        }
        DAIMA_LOGI(TAG, "  %-9s -> %s (from %s)",
                    cfg->profiles[i].name,
                    cfg->profiles[i].model,
                    provider_name);
    }
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
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)kmalloc((size_t)len + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static bool load_json_cfg(category_router_cfg_t *cfg, const char *json_text)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *json_root = root;
    cJSON *category_root = cJSON_GetObjectItem(root, "category_routing");
    if (cJSON_IsObject(category_root)) {
        root = category_root;
    }

    init_empty_cfg(cfg);

    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(enabled)) {
        cfg->enabled = cJSON_IsTrue(enabled);
    }

    bool loaded_categories = false;
    cJSON *categories = cJSON_GetObjectItem(root, "categories");
    cJSON *profiles = cJSON_GetObjectItem(root, "profiles");
    if (cJSON_IsObject(categories)) {
        parse_categories_json(cfg, json_root, categories);
        loaded_categories = cfg->profile_count > 0;
    } else if (cJSON_IsArray(profiles)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, profiles) {
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *model = cJSON_GetObjectItem(item, "model");
            cJSON *context_limit = cJSON_GetObjectItem(item, "context_limit");
            cJSON *max_tokens = cJSON_GetObjectItem(item, "max_tokens");
            if (!cJSON_IsString(name) || !cJSON_IsString(model)) {
                continue;
            }
            add_profile(cfg,
                        name->valuestring,
                        model->valuestring,
                        cJSON_IsNumber(context_limit) ? context_limit->valueint : 0,
                        cJSON_IsNumber(max_tokens) ? max_tokens->valueint : 0);
        }
    } else if (cJSON_IsObject(profiles)) {
        add_profiles_from_provider_object(cfg, profiles);
    }

    if (loaded_categories) {
        log_loaded_categories(cfg, json_root);
    }

    cJSON *intent_map = cJSON_GetObjectItem(root, "intent_map");
    if (cJSON_IsObject(intent_map)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, intent_map) {
            daima_intent_t intent = intent_from_key(entry->string);
            if (intent < 0 || intent >= DAIMA_INTENT_COUNT || !cJSON_IsString(entry)) {
                continue;
            }
            cfg->intent_map[intent] = find_profile_index(cfg, entry->valuestring);
        }
    }

    for (int i = 0; i < AGENT_ROLE_COUNT; i++) {
        cfg->role_model_map[i] = -1;
    }
    cJSON *role_map = cJSON_GetObjectItem(root, "role_model_map");
    if (cJSON_IsObject(role_map)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, role_map) {
            if (!entry->string || !cJSON_IsString(entry)) continue;
            int role_idx = role_from_name(entry->string);
            if (role_idx < 0 || role_idx >= AGENT_ROLE_COUNT) continue;
            cfg->role_model_map[role_idx] = find_profile_index(cfg, entry->valuestring);
        }
    }

    bool ok = cfg->profile_count > 0;
    cJSON_Delete(json_root);
    return ok;
}

static char *read_category_config(char *out_path, size_t out_path_size)
{
    const char *env_home = getenv("DAIMA_HOME");
    const char *config_dir = NULL;
    char env_config_dir[DAIMA_BUF_PATH];

    if (env_home && env_home[0]) {
        snprintf(env_config_dir, sizeof(env_config_dir), "%s/spiffs_data/config", env_home);
        config_dir = env_config_dir;
    } else {
        config_dir = daima_path_config_dir();
    }

    snprintf(out_path, out_path_size, "%s/category_routing.json", config_dir);
    char *json_text = read_file(out_path);
    if (json_text) {
        return json_text;
    }

    snprintf(out_path, out_path_size, "%s/config.json", config_dir);
    return read_file(out_path);
}

category_router_cfg_t category_router_load_and_get_cfg(void)
{
    const char *env_home = getenv("DAIMA_HOME");
    const char *home_key = (env_home && env_home[0]) ? env_home : "";
    if (s_loaded && strcmp(s_loaded_home, home_key) == 0) {
        return s_cfg;
    }

    char path[DAIMA_BUF_PATH];
    char *json_text = read_category_config(path, sizeof(path));
    if (json_text) {
        if (!load_json_cfg(&s_cfg, json_text)) {
            DAIMA_LOGW(TAG, "Invalid category routing config, using defaults: %s", path);
            load_default_cfg(&s_cfg);
        }
        kfree(json_text);
    } else {
        load_default_cfg(&s_cfg);
    }

    s_loaded = true;
    safe_copy(s_loaded_home, sizeof(s_loaded_home), home_key);
    return s_cfg;
}

const daima_category_profile_t *category_router_resolve(daima_intent_t intent)
{
    category_router_load_and_get_cfg();
    if (!s_cfg.enabled || intent < 0 || intent >= DAIMA_INTENT_COUNT) {
        return NULL;
    }

    int profile_index = s_cfg.intent_map[intent];
    if (profile_index < 0 || profile_index >= s_cfg.profile_count) {
        return NULL;
    }
    return &s_cfg.profiles[profile_index];
}

const daima_category_profile_t *category_router_resolve_for_role(agent_role_t role)
{
    category_router_load_and_get_cfg();
    if (!s_cfg.enabled || role < 0 || role >= AGENT_ROLE_COUNT) {
        return NULL;
    }

    int profile_index = s_cfg.role_model_map[role];
    if (profile_index < 0 || profile_index >= s_cfg.profile_count) {
        return category_router_resolve(DAIMA_INTENT_OPEN);
    }
    return &s_cfg.profiles[profile_index];
}

void category_router_reset_for_test(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_loaded = false;
    s_loaded_home[0] = '\0';
}
