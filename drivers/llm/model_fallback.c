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
#include "linux/kernel.h"
static bool add_model(model_fallback_cfg_t *cfg, const char *model);

/**
 * 安全字符串拷贝。确保目标缓冲区始终以 '\0' 结尾。
 */
static void safe_copy(char *dst, size_t dst_size, const char *src)
{
	if (!dst || dst_size == 0) {
		return;
	}
	strscpy(dst, src ? src : "", dst_size);
}

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
 * 仅使用当前生效的 provider 模型作为单一模型列表。
 *
 * @param cfg  输出配置
 */
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

/**
 * 向配置中添加模型名称。自动去重。
 *
 * @param cfg    配置指针
 * @param model  模型名称
 * @return       true 表示成功添加；false 表示已满或已存在
 */
static bool add_model(model_fallback_cfg_t *cfg, const char *model)
{
	if (!cfg || !model || !model[0] || cfg->model_count >= FALLBACK_MAX_MODELS) {
		return false;
	}
	/* 去重：已存在的模型不重复添加 */
	for (int i = 0; i < cfg->model_count; i++) {
		if (strcmp(cfg->models[i], model) == 0) {
			return false;
		}
	}
	safe_copy(cfg->models[cfg->model_count], sizeof(cfg->models[cfg->model_count]), model);
	cfg->model_count++;
	return true;
}

/**
 * 解析模型名称 JSON 数组。
 * 格式：["model_a", "model_b", "model_c"]
 * 首条为 primary，后续为 fallback。
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
			add_model(cfg, item->valuestring);
		}
	}
	return cfg->model_count > 0;
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
	/* fallback_models 优先，其次 models */
	cJSON *models = cJSON_GetObjectItem(root, "fallback_models");
	if (!models) {
		models = cJSON_GetObjectItem(root, "models");
	}

	bool ok = parse_models_array(cfg, models);
	/* 方式 B：从 providers 段收集非 active_provider 的模型 */
	if (!ok && root == json_root) {
		cJSON *active_provider = cJSON_GetObjectItem(root, "active_provider");
		cJSON *providers = cJSON_GetObjectItem(root, "providers");
		if (cJSON_IsString(active_provider) && active_provider->valuestring &&
		    cJSON_IsObject(providers)) {
			memset(cfg, 0, sizeof(*cfg));
			cfg->enabled = true;
			cJSON *provider = NULL;
			/* 遍历 providers 对象的所有键，跳过 active_provider 自身 */
			cJSON_ArrayForEach(provider, providers) {
				if (!provider->string || strcmp(provider->string, active_provider->valuestring) == 0) {
					continue;  /* 跳过当前选中的 provider */
				}
				cJSON *model = cJSON_GetObjectItem(provider, "model");
				if (cJSON_IsString(model) && model->valuestring && model->valuestring[0]) {
					add_model(cfg, model->valuestring);
				}
			}
			ok = cfg->model_count > 0;
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
 * 1. 保存当前主模型名称
 * 2. 用主模型发起 llm_chat_tools
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
                                              llm_response_t *resp)
{
	if (!resp) {
		return ERR_INVALID_ARG;
	}

	/* 保存主模型名称，确保函数返回前恢复 */
	char primary_model[64];
	safe_copy(primary_model, sizeof(primary_model), llm_get_model_name());

	/* 首次尝试：主模型 */
	err_t err = llm_chat_tools(system_prompt, messages, tools_json, resp);
	if (err == 0) {
		llm_set_model(primary_model);  /* 恢复（可能在内部被修改） */
		return 0;
	}

	/* 加载回退配置 */
	model_fallback_cfg_t cfg = model_fallback_load_cfg();
	if (!cfg.enabled || cfg.model_count <= 0) {
		llm_set_model(primary_model);
		return err;
	}

	/* 依次尝试备用模型（跳过 primary 避免重复） */
	err_t last_err = err;
	for (int i = 0; i < cfg.model_count; i++) {
		if (strcmp(cfg.models[i], primary_model) == 0) {
			continue;  /* 跳过主模型（已尝试失败） */
		}

		/* 切换到备用模型重试 */
		llm_set_model(cfg.models[i]);
		last_err = llm_chat_tools(system_prompt, messages, tools_json, resp);
		if (last_err == 0) {
			pr_info("Model fallback: primary失败 -> %s", cfg.models[i]);
			llm_set_model(primary_model);  /* 恢复主模型 */
			return 0;
		}
	}

	/* 全部失败：恢复主模型，返回最后一个错误 */
	llm_set_model(primary_model);
	return last_err;
}
