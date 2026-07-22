/* 预定义 Agent 加载器。
 * 扫描 spiffs_data/agents/<name>/ 目录，
 * 读取 meta.json（结构化元数据）和 system_prompt.md（系统提示词），
 * 组装为 agent_definition_t 并注册到 Agent Registry。
 * 已存在的 agent_id 会被跳过（不覆盖）。 */

#include "agent_loader.h"
#include "registry.h"
#include "paths.h"
#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* 从文件读取文本内容到堆缓冲区（调用者负责 kfree） */
static char *read_file_text(const char *filepath, size_t max_size)
{
	FILE *f = fopen(filepath, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || (size_t)sz > max_size) {
		fclose(f);
		return NULL;
	}

	char *buf = kmalloc((size_t)sz + 1, GFP_KERNEL);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

err_t agent_loader_seed_from_spiffs(void)
{
	char agents_dir[512];
	snprintf(agents_dir, sizeof(agents_dir), "%s/agents", path_spiffs_base());

	DIR *root = opendir(agents_dir);
	if (!root) {
		pr_info("Agent seeder: no agents/ directory at %s, skip", agents_dir);
		return 0;
	}

	int loaded = 0;
	int skipped = 0;
	struct dirent *entry;

	while ((entry = readdir(root)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		/* 每个子目录代表一个 agent */
		char agent_dir[640];
		snprintf(agent_dir, sizeof(agent_dir), "%s/%s", agents_dir, entry->d_name);

		struct stat st;
		if (stat(agent_dir, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;

		/* 1. 读取 meta.json */
		char meta_path[768];
		snprintf(meta_path, sizeof(meta_path), "%s/meta.json", agent_dir);
		char *meta_json = read_file_text(meta_path, 16384);
		if (!meta_json) {
			pr_warn("Agent seeder: no meta.json in %s, skip", agent_dir);
			continue;
		}

		cJSON *root_json = cJSON_Parse(meta_json);
		kfree(meta_json);
		if (!root_json) {
			pr_warn("Agent seeder: invalid JSON in %s/meta.json", agent_dir);
			continue;
		}

		/* 解析结构化字段 */
		agent_definition_t def;
		memset(&def, 0, sizeof(def));

#define GET_STR(field, key) \
		do { const char *_v = cJSON_GetStringValue(cJSON_GetObjectItem(root_json, key)); \
		     if (_v) strscpy(def.field, _v, sizeof(def.field)); } while (0)

#define GET_NUM(field, key) \
		do { cJSON *_item = cJSON_GetObjectItem(root_json, key); \
		     if (_item && cJSON_IsNumber(_item)) \
		         def.field = (typeof(def.field))cJSON_GetNumberValue(_item); } while (0)

		GET_STR(agent_id, "agent_id");
		GET_STR(name, "name");
		GET_STR(description, "description");
		GET_STR(origin, "origin");
		GET_STR(core_skills, "core_skills");
		GET_STR(optional_skills, "optional_skills");
		GET_STR(toolset, "toolset");
		GET_STR(system_prompt, "system_prompt");  /* fallback：若 meta.json 内联了 system_prompt */
		GET_STR(model_provider, "model_provider");
		GET_STR(model_name, "model_name");
		GET_NUM(context_limit, "context_limit");
		GET_NUM(max_tokens, "max_tokens");
		GET_NUM(temperature, "temperature");
		GET_STR(lifecycle_status, "lifecycle_status");
		GET_STR(created_by, "created_by");
		GET_NUM(version, "version");
		GET_NUM(distillation_confidence, "distillation_confidence");

#undef GET_STR
#undef GET_NUM

		cJSON_Delete(root_json);

		/* 基本字段校验 */
		if (!def.agent_id[0] || !def.name[0]) {
			pr_warn("Agent seeder: missing agent_id or name in %s/meta.json", agent_dir);
			continue;
		}

		/* 2. 读取 system_prompt.md（优先于 meta.json 中的内联字段） */
		char prompt_path[768];
		snprintf(prompt_path, sizeof(prompt_path), "%s/system_prompt.md", agent_dir);
		char *prompt_text = read_file_text(prompt_path, AGENT_SYSTEM_PROMPT_LEN - 1);
		if (prompt_text) {
			strscpy(def.system_prompt, prompt_text, sizeof(def.system_prompt));
			kfree(prompt_text);
		}

		/* 最终校验：必须有 system_prompt（来源：system_prompt.md 或 meta.json 内联） */
		if (!def.system_prompt[0]) {
			pr_warn("Agent seeder: no system_prompt for %s, skip", def.agent_id);
			continue;
		}

		/* 默认值 */
		if (!def.lifecycle_status[0])
			strscpy(def.lifecycle_status, "active", sizeof(def.lifecycle_status));
		if (!def.origin[0])
			strscpy(def.origin, "manual", sizeof(def.origin));

		/* 3. 注册 */
		err_t err = agent_registry_register(&def);
		if (err == 0) {
			pr_info("Agent seeder: registered %s (%s)", def.agent_id, def.name);
			loaded++;
		} else if (err == ERR_INVALID_STATE) {
			pr_info("Agent seeder: %s already exists, skip", def.agent_id);
			skipped++;
		} else {
			pr_warn("Agent seeder: register %s failed: %d", def.agent_id, err);
		}
	}

	closedir(root);
	pr_info("Agent seeder: %d loaded, %d skipped from %s", loaded, skipped, agents_dir);
	return 0;
}
