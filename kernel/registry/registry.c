/* Agent Registry 实现：JSON 文件持久化存储。 */
#include "registry.h"
#include "agent_loader.h"

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
#include <sys/types.h>
#include <unistd.h>

static char s_agents_dir[256];

/* ──── 内部路径辅助 ──── */

static void make_agents_dir(void)
{
    snprintf(s_agents_dir, sizeof(s_agents_dir),
             "%s/data/agents", path_memory_dir());
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", s_agents_dir);
    system(cmd);
}

static void agent_dir_path(const char *agent_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s", s_agents_dir, agent_id);
}

static void agent_file_path(const char *agent_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s/agent.json", s_agents_dir, agent_id);
}

static void skills_file_path(const char *agent_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s/skills.json", s_agents_dir, agent_id);
}

/* ──── JSON 序列化/反序列化 ──── */

static cJSON *agent_def_to_json(const agent_definition_t *def)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;

    cJSON_AddStringToObject(root, "agent_id", def->agent_id);
    cJSON_AddStringToObject(root, "name", def->name);
    cJSON_AddStringToObject(root, "description", def->description);
    cJSON_AddStringToObject(root, "origin", def->origin);
    cJSON_AddStringToObject(root, "core_skills", def->core_skills);
    cJSON_AddStringToObject(root, "optional_skills", def->optional_skills);
    cJSON_AddStringToObject(root, "system_prompt", def->system_prompt);
    cJSON_AddStringToObject(root, "toolset", def->toolset);
    cJSON_AddStringToObject(root, "model_provider", def->model_provider);
    cJSON_AddStringToObject(root, "model_name", def->model_name);
    cJSON_AddNumberToObject(root, "context_limit", def->context_limit);
    cJSON_AddNumberToObject(root, "max_tokens", def->max_tokens);
    cJSON_AddNumberToObject(root, "temperature", (double)def->temperature);
    cJSON_AddStringToObject(root, "source_transcript_refs", def->source_transcript_refs);
    cJSON_AddStringToObject(root, "lifecycle_status", def->lifecycle_status);
    cJSON_AddNumberToObject(root, "retired_at", (double)def->retired_at);
    cJSON_AddStringToObject(root, "retired_reason", def->retired_reason);
    cJSON_AddStringToObject(root, "handoff_to_agent_id", def->handoff_to_agent_id);
    cJSON_AddStringToObject(root, "created_by", def->created_by);
    cJSON_AddNumberToObject(root, "created_at", (double)def->created_at);
    cJSON_AddNumberToObject(root, "updated_at", (double)def->updated_at);
    cJSON_AddNumberToObject(root, "version", def->version);
    cJSON_AddNumberToObject(root, "distillation_confidence", (double)def->distillation_confidence);

    return root;
}

static void json_to_agent_def(cJSON *root, agent_definition_t *def)
{
    memset(def, 0, sizeof(*def));

#define GET_STR(field, key) \
    do { const char *_v = cJSON_GetStringValue(cJSON_GetObjectItem(root, key)); \
         if (_v) strscpy(def->field, _v, sizeof(def->field)); } while (0)

#define GET_NUM(field, key) \
    do { cJSON *_item = cJSON_GetObjectItem(root, key); \
         if (_item && cJSON_IsNumber(_item)) \
             def->field = (typeof(def->field))cJSON_GetNumberValue(_item); } while (0)

    GET_STR(agent_id, "agent_id");
    GET_STR(name, "name");
    GET_STR(description, "description");
    GET_STR(origin, "origin");
    GET_STR(core_skills, "core_skills");
    GET_STR(optional_skills, "optional_skills");
    GET_STR(system_prompt, "system_prompt");
    GET_STR(toolset, "toolset");
    GET_STR(model_provider, "model_provider");
    GET_STR(model_name, "model_name");
    GET_NUM(context_limit, "context_limit");
    GET_NUM(max_tokens, "max_tokens");
    GET_NUM(temperature, "temperature");
    GET_STR(source_transcript_refs, "source_transcript_refs");
    GET_STR(lifecycle_status, "lifecycle_status");
    GET_NUM(retired_at, "retired_at");
    GET_STR(retired_reason, "retired_reason");
    GET_STR(handoff_to_agent_id, "handoff_to_agent_id");
    GET_STR(created_by, "created_by");
    GET_NUM(created_at, "created_at");
    GET_NUM(updated_at, "updated_at");
    GET_NUM(version, "version");
    GET_NUM(distillation_confidence, "distillation_confidence");

#undef GET_STR
#undef GET_NUM

    if (!def->lifecycle_status[0])
        strscpy(def->lifecycle_status, "active", sizeof(def->lifecycle_status));
}

/* ──── 文件读写 ──── */

static err_t write_agent_json(const char *agent_id, const agent_definition_t *def)
{
    char dir[512];
    agent_dir_path(agent_id, dir, sizeof(dir));
    mkdir(dir, 0755);

    char filepath[640];
    agent_file_path(agent_id, filepath, sizeof(filepath));

    cJSON *root = agent_def_to_json(def);
    if (!root)
        return ERR_NO_MEM;

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json)
        return ERR_NO_MEM;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        kfree(json);
        return ERR_FAIL;
    }
    fprintf(f, "%s\n", json);
    fclose(f);
    kfree(json);
    return 0;
}

static err_t read_agent_json(const char *agent_id, agent_definition_t *def)
{
    char filepath[640];
    agent_file_path(agent_id, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (!f)
        return ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return ERR_INVALID_SIZE;
    }

    char *buf = kmalloc(sz + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return ERR_NO_MEM;
    }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    if (!root)
        return ERR_FAIL;

    json_to_agent_def(root, def);
    cJSON_Delete(root);
    return 0;
}

static void write_skills_json(const char *agent_id, const agent_definition_t *def)
{
    char spath[640];
    skills_file_path(agent_id, spath, sizeof(spath));
    cJSON *skills = cJSON_CreateObject();
    if (!skills)
        return;

    cJSON_AddStringToObject(skills, "agent_id", def->agent_id);
    cJSON_AddStringToObject(skills, "core_skills", def->core_skills);
    cJSON_AddStringToObject(skills, "optional_skills", def->optional_skills);
    cJSON_AddStringToObject(skills, "source", def->origin);
    cJSON_AddStringToObject(skills, "source_transcript_refs", def->source_transcript_refs);
    cJSON_AddNumberToObject(skills, "distillation_confidence", (double)def->distillation_confidence);

    char *json = cJSON_Print(skills);
    cJSON_Delete(skills);
    if (json) {
        FILE *f = fopen(spath, "w");
        if (f) {
            fprintf(f, "%s\n", json);
            fclose(f);
        }
        kfree(json);
    }
}

/* ──── 匹配辅助 ──── */

static int count_matching_skills(const char *capability_tags, const char *agent_skills)
{
    if (!capability_tags || !agent_skills)
        return 0;
    if (!capability_tags[0] || !agent_skills[0])
        return 0;

    char tags_buf[AGENT_CORE_SKILLS_LEN];
    strscpy(tags_buf, capability_tags, sizeof(tags_buf));

    int matched = 0;
    char *saveptr = NULL;
    char *token = strtok_r(tags_buf, " ", &saveptr);
    while (token) {
        if (strcasestr(agent_skills, token))
            matched++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return matched;
}

static int count_tokens(const char *str)
{
    if (!str || !str[0])
        return 0;
    int count = 0;
    char buf[AGENT_CORE_SKILLS_LEN];
    strscpy(buf, str, sizeof(buf));
    char *saveptr = NULL;
    char *token = strtok_r(buf, " ", &saveptr);
    while (token) {
        count++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return count;
}

/* ──── 公开接口 ──── */

err_t agent_registry_init(void)
{
    make_agents_dir();
    pr_info("Agent Registry initialized at %s", s_agents_dir);

    /* 从 spiffs_data/agents/ 导入预定义 Agent */
    agent_loader_seed_from_spiffs();

    return 0;
}

err_t agent_registry_register(const agent_definition_t *def)
{
    if (!def || !def->agent_id[0] || !def->name[0])
        return ERR_INVALID_ARG;

    /* 检查是否已存在 */
    agent_definition_t existing;
    if (read_agent_json(def->agent_id, &existing) == 0) {
        pr_warn("Agent %s already registered, use update", def->agent_id);
        return ERR_INVALID_STATE;
    }

    agent_definition_t copy = *def;
    if (!copy.lifecycle_status[0])
        strscpy(copy.lifecycle_status, "active", sizeof(copy.lifecycle_status));

    err_t err = write_agent_json(copy.agent_id, &copy);
    if (err == 0) {
        write_skills_json(copy.agent_id, &copy);
        pr_info("Agent registered: %s (%s)", copy.agent_id, copy.name);
    }
    return err;
}

err_t agent_registry_get(const char *agent_id, agent_definition_t *out_def)
{
    if (!agent_id || !out_def)
        return ERR_INVALID_ARG;
    return read_agent_json(agent_id, out_def);
}

err_t agent_registry_list(bool include_retired,
                          agent_definition_t *out_defs,
                          int out_capacity,
                          int *out_count)
{
    *out_count = 0;
    if (out_capacity <= 0 || !out_defs)
        return ERR_INVALID_ARG;

    DIR *dir = opendir(s_agents_dir);
    if (!dir)
        return 0; /* no agents yet */

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *out_count < out_capacity) {
        if (entry->d_name[0] == '.')
            continue;

        agent_definition_t def;
        if (read_agent_json(entry->d_name, &def) != 0)
            continue;

        if (!include_retired && strcmp(def.lifecycle_status, "retired") == 0)
            continue;

        out_defs[*out_count] = def;
        (*out_count)++;
    }
    closedir(dir);
    return 0;
}

err_t agent_registry_find_matches(const char *capability_tags,
                                  float match_threshold,
                                  agent_match_result_t *out_matches,
                                  int out_capacity,
                                  int *out_count)
{
    *out_count = 0;
    if (!capability_tags || !out_matches || out_capacity <= 0)
        return ERR_INVALID_ARG;

    int total_requested = count_tokens(capability_tags);
    if (total_requested == 0)
        return 0;

    /* 列出所有 active agent */
    agent_definition_t agents[AGENT_PROFILES_MAX];
    int agent_count = 0;
    agent_registry_list(false, agents, AGENT_PROFILES_MAX, &agent_count);

    /* 评分并收集 */
    agent_match_result_t scored[AGENT_PROFILES_MAX];
    int scored_count = 0;

    for (int i = 0; i < agent_count; i++) {
        /* 搜索文本 = core_skills + optional_skills + description */
        char search_text[AGENT_CORE_SKILLS_LEN * 2 + AGENT_DESC_LEN];
        snprintf(search_text, sizeof(search_text), "%s %s %s",
                 agents[i].core_skills, agents[i].optional_skills,
                 agents[i].description);

        int matched = count_matching_skills(capability_tags, search_text);
        /* description 中的匹配额外加权 20% */
        char skills_only[AGENT_CORE_SKILLS_LEN * 2];
        snprintf(skills_only, sizeof(skills_only), "%s %s",
                 agents[i].core_skills, agents[i].optional_skills);
        int skills_matched = count_matching_skills(capability_tags, skills_only);
        int desc_matched = matched - skills_matched;
        float score = total_requested > 0
                      ? (float)(skills_matched + desc_matched * 1.2f) / (float)total_requested
                      : 0.0f;

        agent_match_result_t result;
        memset(&result, 0, sizeof(result));
        strscpy(result.agent_id, agents[i].agent_id, sizeof(result.agent_id));
        strscpy(result.agent_name, agents[i].name, sizeof(result.agent_name));
        result.score = score;
        result.matched_skill_count = matched;
        result.total_requested_skills = total_requested;
        result.coverage_ratio = score;
        result.fallback_recommended = score < match_threshold;
        if (result.fallback_recommended) {
            snprintf(result.fallback_reason, sizeof(result.fallback_reason),
                     "Score %.2f below threshold %.2f",
                     (double)score, (double)match_threshold);
        }

        scored[scored_count++] = result;
    }

    /* 冒泡排序降序 */
    for (int i = 0; i < scored_count - 1; i++) {
        for (int j = i + 1; j < scored_count; j++) {
            if (scored[j].score > scored[i].score) {
                agent_match_result_t tmp = scored[i];
                scored[i] = scored[j];
                scored[j] = tmp;
            }
        }
    }

    int limit = scored_count < out_capacity ? scored_count : out_capacity;
    for (int i = 0; i < limit; i++)
        out_matches[i] = scored[i];
    *out_count = limit;
    return 0;
}

err_t agent_registry_update(const char *agent_id, const agent_definition_t *def)
{
    if (!agent_id || !def)
        return ERR_INVALID_ARG;

    agent_definition_t existing;
    if (read_agent_json(agent_id, &existing) != 0)
        return ERR_NOT_FOUND;

    agent_definition_t updated = *def;
    strscpy(updated.agent_id, agent_id, sizeof(updated.agent_id));
    updated.version = existing.version + 1;
    updated.updated_at = time(NULL);

    return write_agent_json(agent_id, &updated);
}

err_t agent_registry_retire(const char *agent_id, const char *reason)
{
    agent_definition_t def;
    err_t err = read_agent_json(agent_id, &def);
    if (err != 0)
        return err;

    strscpy(def.lifecycle_status, "retired", sizeof(def.lifecycle_status));
    def.retired_at = time(NULL);
    if (reason)
        strscpy(def.retired_reason, reason, sizeof(def.retired_reason));

    return write_agent_json(agent_id, &def);
}
