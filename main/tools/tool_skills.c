/* 技能浏览工具实现，参考 Hermes 的 skills_list / skill_view。 */

#include "tools/tool_skills.h"
#include "skills/skill_meta.h"
#include "app/daima_paths.h"
#include "daima_config.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "tool_skills";

#define SKILL_PATH_SIZE 1024
static const daima_tool_t s_skills_list_tool = {
    .name = "skills_list",
    .description = "列出当前已安装的技能。适合先看有哪些技能，再决定是否深入查看。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pattern\":{\"type\":\"string\",\"description\":\"可选过滤关键词；会匹配技能目录名、name、description\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_skills_list_execute,
};

static const daima_tool_t s_skill_view_tool = {
    .name = "skill_view",
    .description = "查看某个技能的主说明或关联文件。适合在任务命中技能后按名称精确读取。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"name\":{\"type\":\"string\",\"description\":\"技能目录名，例如 code-review 或 weather\"},"
        "\"file_path\":{\"type\":\"string\",\"description\":\"可选：技能目录内的相对文件路径；省略时读取 SKILL.md\"}"
        "},"
        "\"required\":[\"name\"]}",
    .execute = tool_skill_view_execute,
};

typedef struct {
    char slug[128];
    char title[128];
    char description[256];
} skill_info_t;

static bool file_exists_regular(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool read_skill_info(const char *skill_name, skill_info_t *info)
{
    if (!skill_name || !info) return false;

    char path[SKILL_PATH_SIZE];
    if (!skill_meta_resolve_path(skill_name, NULL, path, sizeof(path))) {
        return false;
    }
    daima_skill_meta_t meta = {0};
    if (!skill_meta_read_file(path, &meta)) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    snprintf(info->slug, sizeof(info->slug), "%.*s", (int)sizeof(info->slug) - 1, skill_name);
    snprintf(info->title, sizeof(info->title), "%.*s",
             (int)sizeof(info->title) - 1,
             meta.title[0] ? meta.title : skill_name);
    snprintf(info->description, sizeof(info->description), "%.*s",
             (int)sizeof(info->description) - 1,
             meta.description);
    if (!info->description[0]) {
        snprintf(info->description, sizeof(info->description), "（无描述）");
    }
    return true;
}

static int compare_skill_info(const void *a, const void *b)
{
    const skill_info_t *ia = (const skill_info_t *)a;
    const skill_info_t *ib = (const skill_info_t *)b;
    return strcmp(ia->slug, ib->slug);
}

daima_err_t tool_skills_list_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
    DIR *dir = opendir(daima_path_skills_dir());
    if (!dir) {
        snprintf(output, output_size, "错误：无法打开技能目录 %s", daima_path_skills_dir());
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    skill_info_t infos[128];
    int count = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (!skill_meta_validate_name(ent->d_name)) {
            continue;
        }

        char skill_file[SKILL_PATH_SIZE];
        if (!skill_meta_resolve_path(ent->d_name, NULL, skill_file, sizeof(skill_file)) ||
            !file_exists_regular(skill_file)) {
            continue;
        }

        skill_info_t info;
        if (!read_skill_info(ent->d_name, &info)) {
            continue;
        }

        if (pattern && pattern[0]) {
            if (!strstr(ent->d_name, pattern) &&
                !strstr(info.title, pattern) &&
                !strstr(info.description, pattern)) {
                continue;
            }
        }

        infos[count++] = info;
        if (count >= (int)(sizeof(infos) / sizeof(infos[0]))) {
            break;
        }
    }
    closedir(dir);

    qsort(infos, (size_t)count, sizeof(infos[0]), compare_skill_info);

    size_t off = snprintf(output, output_size, "SKILLS (%d)\n", count);
    for (int i = 0; i < count && off < output_size - 1; ++i) {
        off += snprintf(output + off, output_size - off, "- %s | %s: %s\n",
                        infos[i].slug, infos[i].title, infos[i].description);
    }
    if (count == 0) {
        snprintf(output + off, output_size - off, "（未找到技能）\n");
    } else {
        snprintf(output + strlen(output), output_size - strlen(output),
                 "\nHint: 用 skill_view {\"name\":\"技能目录名\"} 查看完整说明。\n");
    }

    DAIMA_LOGI(TAG, "skills_list: pattern=%s count=%d", pattern ? pattern : "(none)", count);
    cJSON_Delete(root);
    return DAIMA_OK;
}

daima_err_t tool_skill_view_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    const char *file_path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_path"));
    if (!skill_meta_validate_name(name)) {
        snprintf(output, output_size, "错误：name 只能包含字母、数字、-、_");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    char resolved[SKILL_PATH_SIZE];
    if (!skill_meta_resolve_path(name, file_path, resolved, sizeof(resolved))) {
        snprintf(output, output_size, "错误：file_path 非法，不能使用绝对路径或 '..'");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!file_exists_regular(resolved)) {
        snprintf(output, output_size, "错误：技能文件不存在：%s", resolved);
        cJSON_Delete(root);
        return DAIMA_ERR_NOT_FOUND;
    }

    FILE *f = fopen(resolved, "r");
    if (!f) {
        snprintf(output, output_size, "错误：无法读取技能文件：%s", resolved);
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    size_t off = snprintf(output, output_size, "SKILL: %s\nFILE: %s\n\n", name, resolved);
    size_t remaining = off < output_size ? (output_size - off - 1) : 0;
    size_t n = remaining > 0 ? fread(output + off, 1, remaining, f) : 0;
    off += n;
    output[off] = '\0';
    bool truncated = remaining > 0 && n == remaining && fgetc(f) != EOF;
    fclose(f);

    if (truncated) {
        snprintf(output + off, output_size - off, "\n\n[Hint] 内容过长，已截断。");
    }

    DAIMA_LOGI(TAG, "skill_view: name=%s file=%s", name, file_path ? file_path : "SKILL.md");
    cJSON_Delete(root);
    return DAIMA_OK;
}

const daima_tool_t *tool_skills_list_definition(void)
{
    return &s_skills_list_tool;
}

const daima_tool_t *tool_skill_view_definition(void)
{
    return &s_skill_view_tool;
}
