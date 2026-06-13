/* 技能浏览工具实现。 */

#include "drivers/tool/tool_skills.h"
#include "drivers/skill/skill_meta.h"
#include "paths.h"
#include "autoconf.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#define SKILL_PATH_SIZE 1024
static const daima_tool_t s_skills_tool = {
    .name = "skills",
    .description = "统一技能浏览工具。action=list 列出当前已安装技能；action=view 查看某个技能的主说明或关联文件。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"list 或 view\"},"
        "\"pattern\":{\"type\":\"string\",\"description\":\"可选过滤关键词；会匹配技能目录名、name、description\"},"
        "\"channel\":{\"type\":\"string\",\"description\":\"可选通道名；填写后只列公共技能和该通道技能，例如 websocket、feishu、pet、vector\"},"
        "\"name\":{\"type\":\"string\",\"description\":\"技能目录名，例如 code-review 或 weather\"},"
        "\"file_path\":{\"type\":\"string\",\"description\":\"可选：技能目录内的相对文件路径；省略时读取 SKILL.md\"}"
        "},"
        "\"required\":[\"action\"]}",
    .execute = tool_skills_execute,
};

typedef struct {
    char slug[128];
    char scope[32];
    char title[128];
    char description[256];
} skill_info_t;

static bool file_exists_regular(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool read_skill_info(const char *skill_name, const char *scope, skill_info_t *info)
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
    snprintf(info->scope, sizeof(info->scope), "%.*s",
             (int)sizeof(info->scope) - 1,
             scope && scope[0] ? scope : "common");
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

static bool add_skill_info(skill_info_t *infos,
                           int *count,
                           int max_count,
                           const char *skill_name,
                           const char *scope,
                           const char *pattern)
{
    if (!infos || !count || *count >= max_count || !skill_name || !skill_meta_validate_name(skill_name)) {
        return false;
    }

    char skill_file[SKILL_PATH_SIZE];
    if (!skill_meta_resolve_path(skill_name, NULL, skill_file, sizeof(skill_file)) ||
        !file_exists_regular(skill_file)) {
        return false;
    }

    skill_info_t info;
    if (!read_skill_info(skill_name, scope, &info)) {
        return false;
    }

    if (pattern && pattern[0]) {
        if (!strstr(skill_name, pattern) &&
            !strstr(info.title, pattern) &&
            !strstr(info.description, pattern)) {
            return false;
        }
    }

    infos[(*count)++] = info;
    return true;
}

static void scan_skill_dir(skill_info_t *infos,
                           int *count,
                           int max_count,
                           const char *prefix,
                           const char *dir_path,
                           const char *scope,
                           const char *channel_filter,
                           const char *pattern)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL && *count < max_count) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        if (prefix && strcmp(prefix, "channels") == 0 && channel_filter && channel_filter[0] &&
            strcmp(ent->d_name, channel_filter) != 0) {
            continue;
        }

        char next_scope[32];
        strscpy(next_scope, scope ? scope : "common", sizeof(next_scope));
        if (prefix && strcmp(prefix, "channels") == 0) {
            snprintf(next_scope, sizeof(next_scope), "channel:%.*s",
                     (int)(sizeof(next_scope) - strlen("channel:") - 1),
                     ent->d_name);
        }

        char rel[SKILL_PATH_SIZE];
        if (prefix && prefix[0]) {
            snprintf(rel, sizeof(rel), "%s/%s", prefix, ent->d_name);
        } else {
            strscpy(rel, ent->d_name, sizeof(rel));
        }

        char entry_path[SKILL_PATH_SIZE];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, ent->d_name);
        if (!skill_meta_validate_name(rel)) {
            continue;
        }

        if ((!prefix || !prefix[0]) && strcmp(ent->d_name, "channels") == 0) {
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                scan_skill_dir(infos, count, max_count, rel, entry_path, "channel", channel_filter, pattern);
            }
            continue;
        }

        if (file_exists_regular(entry_path)) {
            continue;
        }

        char skill_file[SKILL_PATH_SIZE + 16];
        snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", entry_path);
        if (file_exists_regular(skill_file)) {
            add_skill_info(infos, count, max_count, rel, scope, pattern);
            continue;
        }

        struct stat st;
        if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            scan_skill_dir(infos, count, max_count, rel, entry_path, next_scope, channel_filter, pattern);
        }
    }

    closedir(dir);
}

static daima_err_t skills_action_list_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pattern"));
    const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "channel"));
    if (channel && channel[0] && !skill_meta_validate_name(channel)) {
        snprintf(output, output_size, "错误：channel 非法，不能使用绝对路径或 '..'");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }
    DIR *dir = opendir(daima_path_skills_dir());
    if (!dir) {
        snprintf(output, output_size, "错误：无法打开技能目录 %s", daima_path_skills_dir());
        cJSON_Delete(root);
        return DAIMA_FAIL;
    }

    skill_info_t infos[128];
    int count = 0;
    scan_skill_dir(infos,
                   &count,
                   (int)(sizeof(infos) / sizeof(infos[0])),
                   "",
                   daima_path_skills_dir(),
                   "common",
                   channel,
                   pattern);
    closedir(dir);

    qsort(infos, (size_t)count, sizeof(infos[0]), compare_skill_info);

    size_t off = snprintf(output, output_size, "SKILLS (%d)%s%s\n",
                          count,
                          channel && channel[0] ? " channel=" : "",
                          channel && channel[0] ? channel : "");
    for (int i = 0; i < count && off < output_size - 1; ++i) {
        off += snprintf(output + off, output_size - off, "- [%s] %s | %s: %s\n",
                        infos[i].scope, infos[i].slug, infos[i].title, infos[i].description);
    }
    if (count == 0) {
        snprintf(output + off, output_size - off, "（未找到技能）\n");
    } else {
        snprintf(output + strlen(output), output_size - strlen(output),
                 "\nHint: 用 skills {\"action\":\"view\",\"name\":\"技能目录名\"} 查看完整说明。\n");
    }

    pr_info("skills list: channel=%s pattern=%s count=%d", channel ? channel : "(all)", pattern ? pattern : "(none)", count);
    cJSON_Delete(root);
    return DAIMA_OK;
}

static daima_err_t skills_action_view_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    const char *file_path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_path"));
    if (!skill_meta_validate_name(name)) {
        snprintf(output, output_size, "错误：name 只能包含字母、数字、-、_、/，且不能使用绝对路径或 '..'");
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

    pr_info("skills view: name=%s file=%s", name, file_path ? file_path : "SKILL.md");
    cJSON_Delete(root);
    return DAIMA_OK;
}

daima_err_t tool_skills_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    if (!action || !action[0]) {
        snprintf(output, output_size, "错误：缺少 action 字段（list/view）");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_err_t err;
    if (strcmp(action, "list") == 0) {
        err = skills_action_list_execute(input_json, output, output_size);
    } else if (strcmp(action, "view") == 0) {
        err = skills_action_view_execute(input_json, output, output_size);
    } else {
        snprintf(output, output_size, "错误：action 必须是 list 或 view");
        err = DAIMA_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return err;
}

const daima_tool_t *tool_skills_definition(void)
{
    return &s_skills_tool;
}
