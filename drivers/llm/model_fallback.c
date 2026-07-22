/* LLM 模型回退引擎实现。
 * - 当主模型调用失败时自动切换到备用模型链
 * - 配置加载优先级：环境变量 → category_routing.json → fallback_models.json → config.json → 运行时默认
 * - 回退链顺序：primary → fallback[0] → fallback[1] → ...
 * - 成功后恢复主模型名称，避免影响后续调用
 */

#include "drivers/llm/model_fallback.h"

#include "paths.h"
#include "runtime.h"
#include "autoconf.h"
#include "linux/printk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"
#include "text.h"
#include "linux/kernel.h"

/**
 * 读取文件内容到堆内存。
 * 限制最大 128KB，防止异常配置文件消耗过多内存。
 *
 * @param path  文件路径
 * @return      堆分配的文件内容（调用者负责 kfree）；失败返回 NULL
 */
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

/**
 * 加载默认回退配置（无配置文件时）。
 * 仅使用当前生效的 provider 作为单一回退链。
 *
 * @param cfg  输出配置
 */
static void load_default_cfg(model_fallback_cfg_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	const char *active_provider = runtime_config_get_active_provider_name();
	if (!active_provider || !active_provider[0]) {
		pr_warn("No active provider configured, fallback disabled");
		cfg->enabled = false;
		return;
	}

	cfg->enabled = true;
	safe_copy(cfg->providers[0], sizeof(cfg->providers[0]), active_provider);
	cfg->provider_count = 1;

	pr_debug("Model fallback default provider: %s", active_provider);
}

/**
 * 向配置中添加 provider 名称。自动去重。
 *
 * @param cfg    配置指针
 * @param model  模型名称
 * @return       true 表示成功添加；false 表示已满或已存在
 */
static bool add_provider(model_fallback_cfg_t *cfg, const char *provider_name)
{
	if (!cfg || !provider_name || !provider_name[0] || cfg->provider_count >= FALLBACK_MAX_MODELS) {
		return false;
	}
	for (int i = 0; i < cfg->provider_count; i++) {
		if (strcmp(cfg->providers[i], provider_name) == 0) {
			return false;
		}
	}
	safe_copy(cfg->providers[cfg->provider_count], sizeof(cfg->providers[cfg->provider_count]), provider_name);
	cfg->provider_count++;
	return true;
}

static bool find_provider_by_model_name(const char *model_name, char *provider_name, size_t provider_name_size)
{
	if (!model_name || !model_name[0] || !provider_name || provider_name_size == 0) {
		return false;
	}
	for (int i = 0;; i++) {
		const char *candidate = runtime_config_get_provider_name_at(i);
		const char *candidate_model;
		if (!candidate || !candidate[0]) {
			break;
		}
		candidate_model = runtime_config_get_provider_model_for_name(candidate);
		if (candidate_model && strcmp(candidate_model, model_name) == 0) {
			safe_copy(provider_name, provider_name_size, candidate);
			return true;
		}
	}
	return false;
}

/**
 * 解析 provider/model 名称 JSON 数组。
 * 支持：
 * - ["provider_a", "provider_b"]
 * - ["model_a", "model_b"]，会映射回 provider
 *
 * @param cfg     输出配置（先清空）
 * @param models  cJSON 数组
 * @return        true 表示至少解析到一个有效模型
 */
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
			char provider_name[64];
			if (runtime_config_get_provider_model_for_name(item->valuestring)) {
				add_provider(cfg, item->valuestring);
			} else if (find_provider_by_model_name(item->valuestring, provider_name, sizeof(provider_name))) {
				add_provider(cfg, provider_name);
			}
		}
	}
	return cfg->provider_count > 0;
}

/**
 * 从 JSON 配置加载回退链。
 *
 * 支持的 JSON 结构：
 *
 * 方式 A - model_fallback 段（category_routing.json）：
 * {
 *   "model_fallback": {
 *     "enabled": true/false,
 *     "fallback_models": ["model_a", "model_b", ...]
 *   }
 * }
 *
 * 方式 B - providers 段（config.json）：
 * 取非 active_provider 的其他 provider 的 model 字段作为备选
 *
 * @param cfg        输出配置
 * @param json_text  JSON 文本
 * @return           true 表示成功解析到有效配置
 */
static bool load_json_cfg(model_fallback_cfg_t *cfg, const char *json_text)
{
	cJSON *json_root = cJSON_Parse(json_text);
	if (!json_root || !cJSON_IsObject(json_root)) {
		cJSON_Delete(json_root);
		return false;
	}

	cJSON *root = json_root;
	/* 先尝试 model_fallback 子对象（category_routing.json 格式） */
	cJSON *fallback_root = cJSON_GetObjectItem(root, "model_fallback");
	if (cJSON_IsObject(fallback_root)) {
		root = fallback_root;
	}

	cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
	/* fallback_providers 优先，其次 fallback_models / models */
	cJSON *providers = cJSON_GetObjectItem(root, "fallback_providers");
	if (!providers) {
		providers = cJSON_GetObjectItem(root, "providers");
	}
	bool ok = parse_models_array(cfg, providers);
	/* fallback_models 优先，其次 models */
	cJSON *models = cJSON_GetObjectItem(root, "fallback_models");
	if (!models) {
		models = cJSON_GetObjectItem(root, "models");
	}
	if (!ok) {
		ok = parse_models_array(cfg, models);
	}
	/* 方式 B：从 providers 段收集非 active_provider 的模型 */
	if (!ok && root == json_root) {
		cJSON *active_provider = cJSON_GetObjectItem(root, "active_provider");
		cJSON *provider_map = cJSON_GetObjectItem(root, "providers");
		if (cJSON_IsString(active_provider) && active_provider->valuestring &&
		    cJSON_IsObject(provider_map)) {
			memset(cfg, 0, sizeof(*cfg));
			cfg->enabled = true;
			cJSON *provider = NULL;
			cJSON_ArrayForEach(provider, provider_map) {
				if (!provider->string || strcmp(provider->string, active_provider->valuestring) == 0) {
					continue;
				}
				add_provider(cfg, provider->string);
			}
			ok = cfg->provider_count > 0;
		}
	}
	/* enabled 字段覆盖回退开关 */
	if (ok && cJSON_IsBool(enabled)) {
		cfg->enabled = cJSON_IsTrue(enabled);
	}
	cJSON_Delete(json_root);
	return ok;
}

/**
 * 确定配置文件搜索目录。
 * 优先使用 AGENT_HOME 环境变量，回退到默认配置目录。
 */
static const char *config_dir_for_load(char *env_config_dir, size_t env_config_dir_size)
{
	const char *env_home = getenv("AGENT_HOME");
	if (env_home && env_home[0]) {
		snprintf(env_config_dir, env_config_dir_size, "%s/spiffs_data/config", env_home);
		return env_config_dir;
	}
	return path_config_dir();
}

/**
 * 按优先级依次尝试读取回退配置文件。
 *
 * 搜索顺序：
 * 1. category_routing.json（含 model_fallback 段）
 * 2. fallback_models.json（纯模型列表）
 * 3. config.json（从 providers 推断）
 *
 * @param out_path      输出找到的配置文件路径
 * @param out_path_size 路径缓冲区大小
 * @return              文件内容（调用者负责 kfree）；未找到返回 NULL
 */
static char *read_fallback_config(char *out_path, size_t out_path_size)
{
	char env_config_dir[BUF_PATH];
	const char *config_dir = config_dir_for_load(env_config_dir, sizeof(env_config_dir));

	/* 优先级 1：category_routing.json */
	snprintf(out_path, out_path_size, "%s/category_routing.json", config_dir);
	char *json_text = read_file(out_path);
	if (json_text) {
		return json_text;
	}

	/* 优先级 2：fallback_models.json */
	snprintf(out_path, out_path_size, "%s/fallback_models.json", config_dir);
	json_text = read_file(out_path);
	if (json_text) {
		return json_text;
	}

	/* 优先级 3：config.json */
	snprintf(out_path, out_path_size, "%s/config.json", config_dir);
	return read_file(out_path);
}

/**
 * 加载模型回退配置（公开接口）。
 *
 * 加载流程：
 * 1. 检查 MODEL_FALLBACK_ENABLED 环境变量 → 值为 "0" 时直接禁用
 * 2. 按优先级搜索配置文件 → 成功解析 JSON
 * 3. 无配置文件 → 使用运行时默认（active provider model 作为唯一模型）
 *
 * @return 配置结构体（值拷贝，无堆内存）
 */
model_fallback_cfg_t model_fallback_load_cfg(void)
{
	model_fallback_cfg_t cfg;
	/* 环境变量优先：MODEL_FALLBACK_ENABLED=0 强制禁用 */
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

/**
 * 带模型回退的工具调用（公开接口）。
 *
 * 执行流程：
 * 1. 保存当前主 provider/model
 * 2. 用当前主 provider 发起请求
 * 3. 成功 → 恢复主模型名称，返回 0
 * 4. 失败 → 加载回退配置
 * 5. 依次尝试备用模型（跳过与主模型重复的条目）
 * 6. 任一成功 → 恢复主模型名称，返回 0
 * 7. 全部失败 → 恢复主模型名称，返回最后一次错误码
 *
 * 无论成功或失败，都会恢复主模型名称，避免回退模型"污染"后续调用。
 *
 * @param system_prompt  系统提示词
 * @param messages       消息数组
 * @param tools_json     工具定义 JSON（可为 NULL）
 * @param resp           输出响应
 * @return               成功返回 0
 */
err_t model_fallback_chat_with_fallback(const char *system_prompt,
                                        cJSON *messages,
                                        const char *tools_json,
                                        const char *model_override,
                                        bool response_format_json_object,
                                        llm_response_t *resp)
{
	if (!resp) {
		return ERR_INVALID_ARG;
	}

	const char *primary_provider = runtime_config_get_active_provider_name();
	const char *primary_model = (model_override && model_override[0]) ? model_override : llm_get_model_name();
	char resolved_primary_provider[64] = {0};
	const char *primary_provider_for_request = primary_provider;
	if (model_override && model_override[0] &&
	    find_provider_by_model_name(model_override, resolved_primary_provider, sizeof(resolved_primary_provider)) &&
	    resolved_primary_provider[0]) {
		primary_provider_for_request = resolved_primary_provider;
	}

	/* 首次尝试：主模型 */
	err_t err = llm_chat_tools_with_provider_and_format(system_prompt,
	                                                    messages,
	                                                    tools_json,
	                                                    primary_provider_for_request,
	                                                    primary_model,
	                                                    response_format_json_object,
	                                                    resp);
	if (err == 0) {
		return 0;
	}

	/* 加载回退配置 */
	model_fallback_cfg_t cfg = model_fallback_load_cfg();
	if (!cfg.enabled || cfg.provider_count <= 0) {
		return err;
	}

	/* 依次尝试备用 provider（跳过当前 provider 避免重复） */
	err_t last_err = err;
	for (int i = 0; i < cfg.provider_count; i++) {
		const char *provider_name = cfg.providers[i];
		if (!provider_name[0]) {
			continue;
		}
		if (primary_provider_for_request && primary_provider_for_request[0] &&
		    strcmp(provider_name, primary_provider_for_request) == 0) {
			continue;
		}
		last_err = llm_chat_tools_with_provider_and_format(system_prompt,
		                                                   messages,
		                                                   tools_json,
		                                                   provider_name,
		                                                   NULL,
		                                                   response_format_json_object,
		                                                   resp);
		if (last_err == 0) {
			pr_info("Model fallback: primary失败 -> provider %s", provider_name);
			return 0;
		}
	}

	/* 全部失败：返回最后一个错误 */
	return last_err;
}
