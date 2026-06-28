#include "drivers/tool/tool_delegate_repo_batch.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cjson.h"
#include "drivers/tool/tool_invocation_context.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_files.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "lib/text.h"

static bool path_already_selected(char paths[][512], int count, const char *candidate)
{
    if (!paths || !candidate || !candidate[0]) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(paths[i], candidate) == 0) {
            return true;
        }
    }
    return false;
}

static void build_generic_scope_task_description(const char *subagent_type,
                                                 const char *path,
                                                 char *dst,
                                                 size_t dst_size)
{
    const char *leaf = tool_delegate_path_basename(path);
    const char *label = (leaf && leaf[0]) ? leaf : path;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    snprintf(dst,
             dst_size,
             "%s: %.72s",
             subagent_type && subagent_type[0] ? subagent_type : "explore",
             label && label[0] ? label : "(unknown target)");
}

static bool prompt_looks_like_repo_synthesis_request(const char *prompt)
{
    static const char *const keywords[] = {
        "代码框架", "代码架构", "目录结构", "模块", "入口", "主流程", "职责", "阅读顺序", "风险",
        "codebase", "repository", "repo", "architecture", "module", "entry", "read order", "risk"
    };

    if (!prompt || !prompt[0]) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(keywords); i++) {
        if (strstr(prompt, keywords[i])) {
            return true;
        }
    }
    return false;
}

static bool resolve_repo_root_auto_batch_scopes_from_path(const char *repo_root,
                                                          char paths[][512],
                                                          int *out_count)
{
    char children[24][160];
    int child_count = 0;
    int count = 0;
    char input_json[1200];
    char output[4096];
    char *cursor;

    if (!repo_root || !repo_root[0] || !paths || !out_count) {
        return false;
    }
    *out_count = 0;
    if (!tool_delegate_file_is_directory(repo_root)) {
        return false;
    }

    strscpy(paths[count++], repo_root, sizeof(paths[0]));

    snprintf(input_json, sizeof(input_json),
             "{\"action\":\"list\",\"path\":\"%s\"}",
             repo_root);
    if (tool_list_dir_execute(input_json, output, sizeof(output)) != 0) {
        *out_count = count;
        return count >= 3;
    }

    cursor = output;
    while (cursor && *cursor && child_count < (int)ARRAY_SIZE(children)) {
        char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (len > 0 && len < sizeof(children[0]) && cursor[0] == '/') {
            char line[160];
            const char *base = NULL;
            memcpy(line, cursor, len);
            line[len] = '\0';
            if (strcmp(line, repo_root) != 0 && tool_delegate_file_is_directory(line)) {
                base = tool_delegate_path_basename(line);
                if (base && base[0] && base[0] != '.' && strncmp(base, "build", 5) != 0) {
                    strscpy(children[child_count++], line, sizeof(children[0]));
                }
            }
        }
        cursor = line_end ? line_end + 1 : NULL;
    }

    for (int i = 0; i < child_count && count < 4; i++) {
        if (!path_already_selected(paths, count, children[i])) {
            strscpy(paths[count++], children[i], sizeof(paths[0]));
        }
    }

    *out_count = count;
    return count >= 3;
}

static void build_generic_scope_task_key(const char *subagent_type,
                                         const char *path,
                                         int index,
                                         char *dst,
                                         size_t dst_size)
{
    const char *leaf = tool_delegate_path_basename(path);
    const char *label = (leaf && leaf[0]) ? leaf : "scope";
    char normalized[64];
    size_t off = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    memset(normalized, 0, sizeof(normalized));
    for (size_t i = 0; label[i] && off + 1 < sizeof(normalized); i++) {
        char ch = label[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            normalized[off++] = (char)tolower((unsigned char)ch);
        } else if (ch == '-' || ch == '_' || ch == '.') {
            normalized[off++] = '_';
        }
    }
    if (!normalized[0]) {
        snprintf(normalized, sizeof(normalized), "scope_%d", index + 1);
    }
    snprintf(dst,
             dst_size,
             "%s_%s",
             subagent_type && subagent_type[0] ? subagent_type : "explore",
             normalized);
}

static void build_scope_oracle_prompt(const char *user_prompt,
                                      const char *repo_root,
                                      const delegate_request_t *batch_req,
                                      char *dst,
                                      size_t dst_size)
{
    char scopes[1024];

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    memset(scopes, 0, sizeof(scopes));

    if (batch_req) {
        for (int i = 0; i < batch_req->batch_count; i++) {
            const char *path = batch_req->batch_tasks[i].target_path;
            if (!path || !path[0]) {
                continue;
            }
            if (scopes[0]) {
                strlcat(scopes, ", ", sizeof(scopes));
            }
            strlcat(scopes, path, sizeof(scopes));
        }
    }

    snprintf(dst,
             dst_size,
             "基于已经完成的 explore 子任务输出，综合分析仓库 `%s` 并直接产出给父代理可用的最终结论。\n"
             "原始用户请求：%s\n"
             "已拆分探索范围：%s\n"
             "要求：\n"
             "1. 必须等待并使用所有依赖 explore 任务的结果，不要重新并行拆分。\n"
             "2. 重点回答用户真正要的层次职责、入口、主流程、阅读顺序、架构风险。\n"
             "3. 明确哪些判断来自已有子任务证据，避免只复述目录列表。",
             repo_root && repo_root[0] ? repo_root : "(unknown repo)",
             user_prompt && user_prompt[0] ? user_prompt : "",
             scopes[0] ? scopes : "(none)");
}

static bool explicit_scope_is_path_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == '~';
}

static bool explicit_scope_is_separator(char ch)
{
    return !explicit_scope_is_path_char(ch);
}

static bool explicit_scope_is_token_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '/';
}

static bool relative_scope_token_looks_like_path(const char *token)
{
    return token && token[0] &&
           strchr(token, '/') != NULL &&
           strstr(token, "//") == NULL &&
           strstr(token, "..") == NULL;
}

static bool bare_scope_token_looks_like_child_name(const char *token)
{
    return token && token[0] &&
           strchr(token, '/') == NULL &&
           strstr(token, "..") == NULL &&
           strlen(token) >= 2 &&
           strlen(token) < 96;
}

static void trim_scope_token(char *token)
{
    size_t len;

    if (!token || !token[0]) {
        return;
    }

    while (*token == '`' || *token == '"' || *token == '\'' || *token == '(') {
        memmove(token, token + 1, strlen(token));
    }

    len = strlen(token);
    while (len > 0) {
        char ch = token[len - 1];
        if (ch != '/' && !explicit_scope_is_separator(ch)) {
            break;
        }
        token[--len] = '\0';
    }
}

static bool explicit_scope_path_exists(const char *path)
{
    struct stat st;

    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int tool_delegate_collect_message_absolute_directory_paths(const char *content,
                                                           char items[][512],
                                                           int max_count)
{
    const char *cursor;
    int count = 0;

    if (!content || !items || max_count <= 0) {
        return 0;
    }

    cursor = content;
    while (cursor && *cursor && count < max_count) {
        const char *start = strchr(cursor, '/');
        const char *end;
        char path[512];
        size_t len;

        if (!start) {
            break;
        }

        if (start > content) {
            char before = start[-1];
            if (explicit_scope_is_path_char(before) && before != '(' && before != '"' &&
                before != '\'' && before != '`') {
                cursor = start + 1;
                continue;
            }
        }

        end = start;
        while (*end && explicit_scope_is_path_char(*end)) {
            end++;
        }
        len = (size_t)(end - start);
        if (len <= 1 || len >= sizeof(path)) {
            cursor = end;
            continue;
        }

        memcpy(path, start, len);
        path[len] = '\0';
        trim_scope_token(path);
        if (explicit_scope_path_exists(path) && !path_already_selected(items, count, path)) {
            strscpy(items[count++], path, sizeof(items[0]));
        }
        cursor = end;
    }

    return count;
}

static void path_parent_dir(const char *path, char *out, size_t out_size)
{
    char tmp[512];
    char *slash;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !path[0]) {
        return;
    }

    strscpy(tmp, path, sizeof(tmp));
    slash = strrchr(tmp, '/');
    if (!slash) {
        return;
    }
    if (slash == tmp) {
        strscpy(out, "/", out_size);
        return;
    }
    *slash = '\0';
    strscpy(out, tmp, out_size);
}

static void infer_common_existing_parent(char paths[][512], int count, char *out, size_t out_size)
{
    char candidate[512];

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!paths || count <= 0) {
        return;
    }

    strscpy(candidate, paths[0], sizeof(candidate));
    while (candidate[0]) {
        bool all_match = true;

        for (int i = 1; i < count; i++) {
            size_t len = strlen(candidate);
            if (strncmp(paths[i], candidate, len) != 0 ||
                (paths[i][len] != '\0' && paths[i][len] != '/')) {
                all_match = false;
                break;
            }
        }
        if (all_match && explicit_scope_path_exists(candidate)) {
            strscpy(out, candidate, out_size);
            return;
        }
        path_parent_dir(candidate, candidate, sizeof(candidate));
    }
}

static void extract_first_absolute_repo_path(const char *content, char *root, size_t root_size)
{
    char paths[16][512];
    int count;

    if (!root || root_size == 0) {
        return;
    }
    root[0] = '\0';
    if (!content || !content[0]) {
        return;
    }

    count = tool_delegate_collect_message_absolute_directory_paths(content, paths, ARRAY_SIZE(paths));
    if (count >= 2) {
        infer_common_existing_parent(paths, count, root, root_size);
        if (root[0]) {
            return;
        }
    }
    if (count >= 1) {
        strscpy(root, paths[0], root_size);
        return;
    }
}

static bool explicit_scope_exists(const explicit_multi_scope_group_t *group, const char *path)
{
    if (!group || !path || !path[0]) {
        return false;
    }
    for (int i = 0; i < group->count; i++) {
        if (strcmp(group->paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static void explicit_scope_add(explicit_multi_scope_group_t *group, const char *path)
{
    if (!group || !path || !path[0] || group->count >= (int)ARRAY_SIZE(group->paths)) {
        return;
    }
    if (explicit_scope_exists(group, path)) {
        return;
    }
    strscpy(group->paths[group->count++], path, sizeof(group->paths[0]));
}

static bool prompt_rewrite_replace_first_path(const char *src,
                                              const char *from_path,
                                              const char *to_path,
                                              char *dst,
                                              size_t dst_size)
{
    const char *pos;
    size_t prefix_len;

    if (!src || !src[0] || !from_path || !from_path[0] ||
        !to_path || !to_path[0] || !dst || dst_size == 0) {
        return false;
    }

    pos = strstr(src, from_path);
    if (!pos) {
        return false;
    }

    prefix_len = (size_t)(pos - src);
    if (prefix_len + strlen(to_path) + strlen(pos + strlen(from_path)) + 1 > dst_size) {
        return false;
    }

    memcpy(dst, src, prefix_len);
    dst[prefix_len] = '\0';
    strlcat(dst, to_path, dst_size);
    strlcat(dst, pos + strlen(from_path), dst_size);
    return true;
}

static void build_scoped_subagent_prompt(const char *user_prompt,
                                         const char *primary_path,
                                         const char *target_path,
                                         char *dst,
                                         size_t dst_size)
{
    char abs_paths[16][512];
    int abs_count;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';

    abs_count = tool_delegate_collect_message_absolute_directory_paths(user_prompt, abs_paths, ARRAY_SIZE(abs_paths));
    if (abs_count >= 2 && target_path && target_path[0]) {
        snprintf(dst,
                 dst_size,
                 "只负责目标路径 `%s` 这一部分任务。原始用户请求：%s\n"
                 "要求：\n"
                 "1. 严格围绕 `%s` 完成与原始请求直接相关的分析或执行，不要擅自改写任务目标。\n"
                 "2. 其他并列路径只作为上下文引用，不要把它们当成当前主处理对象。\n"
                 "3. 不要再次把这条请求拆成多个并行子任务。",
                 target_path,
                 user_prompt && user_prompt[0] ? user_prompt : "",
                 target_path);
        return;
    }

    if (user_prompt && user_prompt[0] &&
        primary_path && primary_path[0] &&
        target_path && target_path[0] &&
        strcmp(primary_path, target_path) != 0 &&
        prompt_rewrite_replace_first_path(user_prompt,
                                          primary_path,
                                          target_path,
                                          dst,
                                          dst_size)) {
        return;
    }

    if (user_prompt && user_prompt[0]) {
        snprintf(dst,
                 dst_size,
                 "围绕目标路径 `%s` 完成这部分任务：%s",
                 target_path && target_path[0] ? target_path : "(unknown target)",
                 user_prompt);
        return;
    }

    snprintf(dst,
             dst_size,
             "围绕目标路径 `%s` 完成对应子任务。",
             target_path && target_path[0] ? target_path : "(unknown target)");
}

static const char *infer_scope_subagent_type_from_user_prompt(const char *prompt);

static bool generic_task_line_has_work_signal(const char *line)
{
    static const char *const work_keywords[] = {
        "分析", "检查", "排查", "比较", "整理", "归纳", "验证", "实现", "修改", "修复",
        "重构", "编写", "补充", "梳理", "总结", "定位", "review", "analyze", "inspect",
        "check", "compare", "organize", "verify", "implement", "change", "fix",
        "refactor", "write", "summarize", "audit", "investigate"
    };

    if (!line || !line[0]) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(work_keywords); i++) {
        if (strstr(line, work_keywords[i])) {
            return true;
        }
    }
    return false;
}

static void trim_ascii_whitespace_inplace(char *text)
{
    size_t start = 0;
    size_t end;

    if (!text || !text[0]) {
        return;
    }

    end = strlen(text);
    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r') {
        start++;
    }
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) {
        end--;
    }
    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static void strip_task_bullet_prefix(char *text)
{
    size_t idx = 0;

    if (!text || !text[0]) {
        return;
    }

    while (text[idx] == ' ' || text[idx] == '\t') {
        idx++;
    }
    while (text[idx] >= '0' && text[idx] <= '9') {
        idx++;
    }
    if ((idx > 0 && text[idx] == '.') ||
        text[idx] == '-' || text[idx] == '*' ||
        text[idx] == ')' || text[idx] == ':') {
        idx++;
    }
    while (text[idx] == ' ' || text[idx] == '\t') {
        idx++;
    }
    if (idx > 0) {
        memmove(text, text + idx, strlen(text + idx) + 1);
    }
    trim_ascii_whitespace_inplace(text);
}

static void build_generic_task_key_from_index(int index, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "task_%d", index + 1);
}

static void build_generic_batch_task_prompt(const char *full_prompt,
                                            const char *task_line,
                                            char *dst,
                                            size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    snprintf(dst,
             dst_size,
             "你只负责这一个子任务，不要擅自扩展到其他并列任务，也不要再次拆分并行。\n"
             "原始用户请求：%s\n"
             "当前子任务：%s",
             full_prompt && full_prompt[0] ? full_prompt : "",
             task_line && task_line[0] ? task_line : "");
}

static bool collect_generic_task_lines_from_prompt(const char *content,
                                                   char tasks[][512],
                                                   int max_tasks)
{
    const char *cursor;
    int count = 0;

    if (!content || !content[0] || !tasks || max_tasks <= 0) {
        return false;
    }

    cursor = content;
    while (*cursor && count < max_tasks) {
        const char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        char line[512];

        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, cursor, len);
        line[len] = '\0';
        strip_task_bullet_prefix(line);
        if (line[0] &&
            strchr(line, '/') == NULL &&
            generic_task_line_has_work_signal(line) &&
            !path_already_selected(tasks, count, line)) {
            strscpy(tasks[count++], line, sizeof(tasks[0]));
        }
        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }

    return count >= 2;
}

char *tool_delegate_build_user_prompt_generic_delegate_batch_json(const struct message *msg)
{
    char task_lines[8][512];
    cJSON *root = NULL;
    cJSON *tasks = NULL;

    if (!msg || !msg->content || !collect_generic_task_lines_from_prompt(msg->content,
                                                                          task_lines,
                                                                          ARRAY_SIZE(task_lines))) {
        return NULL;
    }

    root = cJSON_CreateObject();
    tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < (int)ARRAY_SIZE(task_lines) && task_lines[i][0]; i++) {
        char prompt[2048];
        char task_key[64];
        const char *subagent_type = infer_scope_subagent_type_from_user_prompt(task_lines[i]);
        cJSON *item = cJSON_CreateObject();

        if (!item) {
            continue;
        }
        build_generic_batch_task_prompt(msg->content, task_lines[i], prompt, sizeof(prompt));
        build_generic_task_key_from_index(i, task_key, sizeof(task_key));
        cJSON_AddStringToObject(item, "task_key", task_key);
        cJSON_AddStringToObject(item, "description", task_lines[i]);
        cJSON_AddStringToObject(item, "subagent_type", subagent_type);
        cJSON_AddStringToObject(item, "prompt", prompt);
        cJSON_AddItemToArray(tasks, item);
    }

    if (cJSON_GetArraySize(tasks) < 2) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    cJSON_AddStringToObject(root, "dispatch_mode", "parallel");
    cJSON_AddItemToObject(root, "tasks", tasks);
    {
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return json;
    }
}

char *tool_delegate_build_repo_analysis_delegate_batch_json(const struct message *msg)
{
    char repo_root[512];
    cJSON *root = NULL;
    cJSON *tasks = NULL;
    cJSON *depends = NULL;
    char *json = NULL;
    static const struct {
        const char *task_key;
        const char *subagent_type;
        const char *description;
        const char *prompt_template;
    } task_templates[] = {
        {
            "cli_entry",
            "explore",
            "分析 CLI 入口",
            "只分析仓库 `%s` 的 CLI/启动入口层。\n"
            "原始用户请求：%s\n"
            "要求：\n"
            "1. 只找入口文件、命令行参数解析、子命令/主调度入口。\n"
            "2. 优先看 README、package manifest、main/cli/command 相关入口，不要做大范围深挖。\n"
            "3. 输出要直接回答：CLI 从哪里进入、如何分发、建议先读哪些文件。"
        },
        {
            "core_exec",
            "explore",
            "分析核心执行层",
            "只分析仓库 `%s` 的核心执行层。\n"
            "原始用户请求：%s\n"
            "要求：\n"
            "1. 只找任务执行主链路、核心 runtime/executor/service 层。\n"
            "2. 先用目录/文件名缩小范围，再读少量关键文件，不要把整个仓库读穿。\n"
            "3. 输出要直接回答：核心执行层职责、主流程、关键模块和建议阅读顺序。"
        },
        {
            "tools_layer",
            "explore",
            "分析工具层",
            "只分析仓库 `%s` 的工具层/能力适配层。\n"
            "原始用户请求：%s\n"
            "要求：\n"
            "1. 只找工具注册、调用适配、driver/tooling/mcp 类模块。\n"
            "2. 重点提炼工具层职责边界，不要泛化成整个仓库总结。\n"
            "3. 输出要直接回答：工具层职责、关键模块、与核心执行层的关系。"
        },
        {
            "docs_layer",
            "librarian",
            "分析文档层",
            "只分析仓库 `%s` 的文档层和说明材料。\n"
            "原始用户请求：%s\n"
            "要求：\n"
            "1. 优先看 README、AGENTS、docs 目录和安装/配置说明。\n"
            "2. 不要转去扫实现细节，重点提炼文档层覆盖了哪些主题。\n"
            "3. 输出要直接回答：文档层职责、关键说明入口、适合先读哪些文档。"
        }
    };

    if (!msg || !msg->content || !msg->content[0]) {
        return NULL;
    }
    if (!tool_delegate_extract_single_absolute_repo_path(msg->content, repo_root, sizeof(repo_root)) ||
        !repo_root[0] ||
        !tool_delegate_file_is_directory(repo_root)) {
        return NULL;
    }

    root = cJSON_CreateObject();
    tasks = cJSON_CreateArray();
    depends = cJSON_CreateArray();
    if (!root || !tasks || !depends) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        cJSON_Delete(depends);
        return NULL;
    }

    for (size_t i = 0; i < ARRAY_SIZE(task_templates); i++) {
        cJSON *task = cJSON_CreateObject();
        char prompt[2048];

        if (!task) {
            continue;
        }
        snprintf(prompt,
                 sizeof(prompt),
                 task_templates[i].prompt_template,
                 repo_root,
                 msg->content);
        cJSON_AddStringToObject(task, "task_key", task_templates[i].task_key);
        cJSON_AddStringToObject(task, "subagent_type", task_templates[i].subagent_type);
        cJSON_AddStringToObject(task, "description", task_templates[i].description);
        cJSON_AddStringToObject(task, "target_path", repo_root);
        cJSON_AddStringToObject(task, "prompt", prompt);
        cJSON_AddItemToArray(tasks, task);
        cJSON_AddItemToArray(depends, cJSON_CreateString(task_templates[i].task_key));
    }

    {
        cJSON *oracle = cJSON_CreateObject();
        char oracle_prompt[4096];

        if (!oracle) {
            cJSON_Delete(root);
            cJSON_Delete(tasks);
            cJSON_Delete(depends);
            return NULL;
        }
        snprintf(oracle_prompt,
                 sizeof(oracle_prompt),
                 "基于已经完成的子任务结果，综合分析仓库 `%s`。\n"
                 "原始用户请求：%s\n"
                 "要求：\n"
                 "1. 必须等待并综合 cli_entry、core_exec、tools_layer、docs_layer 的结果。\n"
                 "2. 最终直接回答用户要的四部分：CLI 入口、核心执行层、工具层、文档层职责。\n"
                 "3. 给出一份简洁的建议阅读顺序。\n"
                 "4. 不要重新大范围搜索仓库，重点做汇总、裁决、去重和结构化输出。",
                 repo_root,
                 msg->content);
        cJSON_AddStringToObject(oracle, "task_key", "oracle_synthesis");
        cJSON_AddStringToObject(oracle, "subagent_type", "oracle");
        cJSON_AddStringToObject(oracle, "description", "汇总仓库架构结论");
        cJSON_AddStringToObject(oracle, "target_path", repo_root);
        cJSON_AddStringToObject(oracle, "prompt", oracle_prompt);
        cJSON_AddItemToObject(oracle, "depends_on", depends);
        cJSON_AddItemToArray(tasks, oracle);
    }

    cJSON_AddStringToObject(root, "dispatch_mode", "staged");
    cJSON_AddItemToObject(root, "tasks", tasks);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static const char *infer_scope_subagent_type_from_user_prompt(const char *prompt)
{
    static const char *const structure_mapping_keywords[] = {
        "代码框架", "代码结构", "项目结构", "目录结构", "仓库结构", "模块划分",
        "模块结构", "架构概览", "结构分析", "框架分析",
        "code framework", "code structure", "project structure", "repo structure",
        "directory structure", "module structure", "architecture overview", "structure analysis"
    };
    static const char *const implement_keywords[] = {
        "修", "修改", "实现", "重构", "补", "新增", "fix", "implement", "refactor", "change", "edit"
    };
    static const char *const oracle_keywords[] = {
        "架构", "设计", "原理", "依赖", "调用链", "数据流", "主流程",
        "评估", "方案", "改造", "入口", "验证", "顺序", "建议",
        "architecture", "design", "dependency", "evaluate", "evaluation",
        "approach", "migration", "entrypoint", "validate", "verification", "sequence", "plan"
    };
    static const char *const librarian_keywords[] = {
        "文档", "文献", "规范", "协议", "api", "API", "readme", "README", "docs", "documentation"
    };
    static const char *const explore_keywords[] = {
        "分析", "检查", "排查", "对比", "比较", "梳理", "归纳", "汇总", "摸底",
        "调研", "核对", "查看", "审计", "定位", "问题", "风险", "优先级", "残留",
        "analyze", "analysis", "inspect", "check", "compare", "review", "audit",
        "investigate", "survey", "summarize", "risk", "issue", "priority"
    };

    if (!prompt || !prompt[0]) {
        return "explore";
    }
    for (size_t i = 0; i < ARRAY_SIZE(structure_mapping_keywords); i++) {
        if (strstr(prompt, structure_mapping_keywords[i])) {
            return "explore";
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(implement_keywords); i++) {
        if (strstr(prompt, implement_keywords[i])) {
            return "implement";
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(oracle_keywords); i++) {
        if (strstr(prompt, oracle_keywords[i])) {
            return "oracle";
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(librarian_keywords); i++) {
        if (strstr(prompt, librarian_keywords[i])) {
            return "librarian";
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(explore_keywords); i++) {
        if (strstr(prompt, explore_keywords[i])) {
            return "explore";
        }
    }
    return "explore";
}

static bool path_is_same_or_child_of(const char *path, const char *root)
{
    size_t root_len;

    if (!path || !path[0] || !root || !root[0]) {
        return false;
    }
    if (strcmp(path, root) == 0) {
        return true;
    }
    root_len = strlen(root);
    return strncmp(path, root, root_len) == 0 && path[root_len] == '/';
}

char *tool_delegate_build_scoped_delegate_batch_json_for_paths(const char *user_prompt,
                                                               const char *primary_path,
                                                               char paths[][512],
                                                               int path_count)
{
    const char *subagent_type;
    cJSON *root = NULL;
    cJSON *tasks = NULL;
    char task_keys[DELEGATE_COORDINATOR_AGENTS_MAX][64];
    bool add_oracle = false;

    if (!paths || path_count < 2) {
        return NULL;
    }

    subagent_type = infer_scope_subagent_type_from_user_prompt(user_prompt);
    root = cJSON_CreateObject();
    tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < path_count; i++) {
        const char *path = paths[i];
        char description[96];
        char prompt[2048];
        char task_key[64];
        cJSON *item = cJSON_CreateObject();

        if (!path[0] || !item) {
            cJSON_Delete(item);
            continue;
        }

        build_generic_scope_task_description(subagent_type,
                                             path,
                                             description,
                                             sizeof(description));
        build_scoped_subagent_prompt(user_prompt,
                                     primary_path,
                                     path,
                                     prompt,
                                     sizeof(prompt));
        build_generic_scope_task_key(subagent_type, path, i, task_key, sizeof(task_key));
        cJSON_AddStringToObject(item, "subagent_type", subagent_type);
        cJSON_AddStringToObject(item, "task_key", task_key);
        cJSON_AddStringToObject(item, "description", description);
        cJSON_AddStringToObject(item, "prompt", prompt);
        cJSON_AddStringToObject(item, "target_path", path);
        cJSON_AddItemToArray(tasks, item);
        strscpy(task_keys[i], task_key, sizeof(task_keys[i]));
    }

    if (cJSON_GetArraySize(tasks) < 2) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    add_oracle = prompt_looks_like_repo_synthesis_request(user_prompt);
    if (add_oracle) {
        cJSON *item = cJSON_CreateObject();
        cJSON *depends = cJSON_CreateArray();
        char oracle_prompt[4096];

        if (item && depends) {
            build_scope_oracle_prompt(user_prompt, primary_path, NULL, oracle_prompt, sizeof(oracle_prompt));
            cJSON_AddStringToObject(item, "task_key", "oracle_synthesis");
            cJSON_AddStringToObject(item, "subagent_type", "oracle");
            cJSON_AddStringToObject(item, "description", "synthesize architecture findings");
            cJSON_AddStringToObject(item, "prompt", oracle_prompt);
            for (int i = 0; i < path_count; i++) {
                if (task_keys[i][0]) {
                    cJSON_AddItemToArray(depends, cJSON_CreateString(task_keys[i]));
                }
            }
            cJSON_AddItemToObject(item, "depends_on", depends);
            cJSON_AddItemToArray(tasks, item);
            cJSON_AddStringToObject(root, "dispatch_mode", "staged");
        } else {
            cJSON_Delete(item);
            cJSON_Delete(depends);
        }
    }
    if (!cJSON_GetObjectItem(root, "dispatch_mode")) {
        cJSON_AddStringToObject(root, "dispatch_mode", "parallel");
    }
    cJSON_AddItemToObject(root, "tasks", tasks);
    {
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return json;
    }
}

static void maybe_add_relative_scope_token(const char *content,
                                           const char *root,
                                           explicit_multi_scope_group_t *out,
                                           const char *needle)
{
    const char *pos = content;

    if (!content || !root || !root[0] || !out || !needle || !needle[0]) {
        return;
    }

    while ((pos = strstr(pos, needle)) != NULL) {
        char before = (pos == content) ? '\0' : pos[-1];
        char after = pos[strlen(needle)];
        char path[512];

        if ((before == '/' && pos > content) || before == '_' || before == '-') {
            pos += strlen(needle);
            continue;
        }
        if (explicit_scope_is_token_char(before) && before != '/') {
            pos += strlen(needle);
            continue;
        }
        if (explicit_scope_is_token_char(after) && after != '/') {
            pos += strlen(needle);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", root, needle);
        trim_scope_token(path);
        if (explicit_scope_path_exists(path)) {
            explicit_scope_add(out, path);
        }
        pos += strlen(needle);
    }
}

static void maybe_add_child_scope_token(const char *content,
                                        const char *root,
                                        explicit_multi_scope_group_t *out,
                                        const char *needle)
{
    const char *pos = content;

    if (!content || !root || !root[0] || !out || !needle || !needle[0]) {
        return;
    }

    while ((pos = strstr(pos, needle)) != NULL) {
        char before = (pos == content) ? '\0' : pos[-1];
        char after = pos[strlen(needle)];
        char path[512];

        if (explicit_scope_is_token_char(before) || explicit_scope_is_token_char(after)) {
            pos += strlen(needle);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", root, needle);
        trim_scope_token(path);
        if (explicit_scope_path_exists(path)) {
            explicit_scope_add(out, path);
        }
        pos += strlen(needle);
    }
}

static void extract_relative_scope_paths(const char *content,
                                         const char *root,
                                         explicit_multi_scope_group_t *out)
{
    const char *cursor;

    if (!content || !root || !root[0] || !out) {
        return;
    }

    cursor = content;
    while (cursor && *cursor) {
        while (*cursor && explicit_scope_is_separator(*cursor)) {
            cursor++;
        }
        if (!*cursor) {
            break;
        }

        const char *end = cursor;
        char token[160];
        size_t len;

        while (*end && !explicit_scope_is_separator(*end)) {
            end++;
        }
        len = (size_t)(end - cursor);
        if (len > 0 && len < sizeof(token)) {
            memcpy(token, cursor, len);
            token[len] = '\0';
            trim_scope_token(token);
            if (relative_scope_token_looks_like_path(token)) {
                maybe_add_relative_scope_token(content, root, out, token);
            } else if (bare_scope_token_looks_like_child_name(token)) {
                maybe_add_child_scope_token(content, root, out, token);
            }
        }
        cursor = end;
    }
}

static void extract_absolute_scope_paths(const char *content,
                                         const char *repo_root,
                                         explicit_multi_scope_group_t *out)
{
    const char *start = content;

    (void)repo_root;

    if (!content || !out) {
        return;
    }

    while ((start = strchr(start, '/')) != NULL) {
        const char *end = start;
        char path[512];

        while (*end && explicit_scope_is_path_char(*end)) {
            end++;
        }
        if ((size_t)(end - start) <= 1 || (size_t)(end - start) >= sizeof(path)) {
            start = end;
            continue;
        }

        memcpy(path, start, (size_t)(end - start));
        path[end - start] = '\0';
        trim_scope_token(path);
        if (strchr(path + 1, '/') &&
            explicit_scope_path_exists(path)) {
            explicit_scope_add(out, path);
        }
        start = end;
    }
}

bool tool_delegate_message_has_multiple_explicit_targets(const struct message *msg)
{
    char abs_paths[8][512];
    char repo_root[512];
    explicit_multi_scope_group_t group = {0};
    int abs_count;

    if (!msg || !msg->content) {
        return false;
    }

    abs_count = tool_delegate_collect_message_absolute_directory_paths(msg->content,
                                                                       abs_paths,
                                                                       ARRAY_SIZE(abs_paths));
    if (abs_count >= 2) {
        return true;
    }

    repo_root[0] = '\0';
    if ((tool_delegate_extract_single_absolute_repo_path(msg->content,
                                                         repo_root,
                                                         sizeof(repo_root)) ||
         tool_delegate_workspace_repo_root_from_prompt(msg->content,
                                                       repo_root,
                                                       sizeof(repo_root))) &&
        repo_root[0]) {
        extract_relative_scope_paths(msg->content, repo_root, &group);
        if (group.count >= 2) {
            return true;
        }
    }

    return false;
}

bool tool_delegate_collect_explicit_multi_scope_paths_from_message(const struct message *msg,
                                                                   explicit_multi_scope_group_t *out)
{
    const char *content;
    char root[512];

    if (!msg || !out) {
        return false;
    }
    if (tool_invocation_context_message_is_delegate_subagent(msg)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    content = msg->content ? msg->content : "";
    extract_first_absolute_repo_path(content, root, sizeof(root));
    extract_absolute_scope_paths(content, root, out);
    extract_relative_scope_paths(content, root, out);
    if (out->count >= 1) {
        for (int i = 0; i < out->count; i++) {
            pr_info("explicit multi-scope collect: chat=%s root=%s idx=%d path=%s",
                    msg->chat_id,
                    root[0] ? root : "<none>",
                    i,
                    out->paths[i]);
        }
    } else {
        pr_info("explicit multi-scope collect: chat=%s root=%s no_paths",
                msg->chat_id,
                root[0] ? root : "<none>");
    }
    return out->count >= 2;
}

char *tool_delegate_build_user_prompt_scoped_delegate_batch_json(const struct message *msg)
{
    char primary_path[512];
    explicit_multi_scope_group_t group;
    const char *subagent_type;
    char paths[16][512];
    int path_count = 0;
    bool has_child_scopes = false;

    if (!msg || !msg->content || !tool_delegate_collect_explicit_multi_scope_paths_from_message(msg, &group) ||
        group.count < 2) {
        return NULL;
    }

    primary_path[0] = '\0';
    extract_first_absolute_repo_path(msg->content, primary_path, sizeof(primary_path));
    subagent_type = infer_scope_subagent_type_from_user_prompt(msg->content);
    pr_info("scoped delegate batch build: chat=%s primary=%s subagent=%s count=%d",
            msg->chat_id,
            primary_path[0] ? primary_path : "<none>",
            subagent_type,
            group.count);

    for (int i = 0; i < group.count; i++) {
        const char *path = group.paths[i];
        if (primary_path[0] && path[0] &&
            strcmp(path, primary_path) != 0 &&
            path_is_same_or_child_of(path, primary_path)) {
            has_child_scopes = true;
        }
    }

    for (int i = 0; i < group.count; i++) {
        const char *path = group.paths[i];
        char description[96];

        if (has_child_scopes && primary_path[0] && strcmp(path, primary_path) == 0) {
            continue;
        }
        build_generic_scope_task_description(subagent_type, path, description, sizeof(description));
        pr_info("scoped delegate batch task: chat=%s idx=%d path=%s description=%s",
                msg->chat_id,
                path_count,
                path,
                description);
        strscpy(paths[path_count++], path, sizeof(paths[0]));
    }
    return tool_delegate_build_scoped_delegate_batch_json_for_paths(msg->content,
                                                                    primary_path,
                                                                    paths,
                                                                    path_count);
}

static bool prompt_has_multiple_repo_relative_scope_mentions(const char *text,
                                                             const char *repo_root)
{
    const char *repo_name;
    char needle[160];
    const char *cursor;
    int hits = 0;

    if (!text || !text[0] || !repo_root || !repo_root[0]) {
        return false;
    }

    repo_name = tool_delegate_path_basename(repo_root);
    if (!repo_name || !repo_name[0] ||
        snprintf(needle, sizeof(needle), "%s/", repo_name) >= (int)sizeof(needle)) {
        return false;
    }

    cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        hits++;
        if (hits >= 2) {
            return true;
        }
        cursor += strlen(needle);
    }

    return false;
}

static bool collect_explicit_prompt_paths(const delegate_request_t *req,
                                          char paths[][512],
                                          int *out_count)
{
    const char *text = NULL;
    char repo_root[512];
    const char *cursor = NULL;
    int count = 0;

    if (!req || !paths || !out_count) {
        return false;
    }
    *out_count = 0;
    text = req->prompt[0] ? req->prompt : req->description;
    if (!text || !text[0] ||
        !tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root)) ||
        !repo_root[0]) {
        return false;
    }

    strscpy(paths[count++], repo_root, sizeof(paths[0]));
    cursor = text;
    while (cursor && *cursor && count < 4) {
        char normalized[512];
        const char *repo_hit = strstr(cursor, tool_delegate_path_basename(repo_root));

        if (!repo_hit) {
            break;
        }
        if (!tool_delegate_extract_repo_scoped_path(repo_hit, repo_root, normalized, sizeof(normalized)) ||
            !normalized[0] ||
            strcmp(normalized, repo_root) == 0 ||
            strncmp(normalized, repo_root, strlen(repo_root)) != 0 ||
            path_already_selected(paths, count, normalized)) {
            cursor = repo_hit + strlen(tool_delegate_path_basename(repo_root));
            continue;
        }

        strscpy(paths[count++], normalized, sizeof(paths[0]));
        cursor = repo_hit + strlen(tool_delegate_path_basename(repo_root));
    }

    if (count < 2 && prompt_has_multiple_repo_relative_scope_mentions(text, repo_root)) {
        return true;
    }
    *out_count = count;
    return count >= 2;
}

static bool resolve_repo_root_auto_batch_scopes(const delegate_request_t *req,
                                                char paths[][512],
                                                int *out_count)
{
    char repo_root[512];
    char children[24][160];
    int child_count = 0;
    int count = 0;
    char input_json[1200];
    char output[4096];
    char *cursor;

    if (!req || !paths || !out_count) {
        return false;
    }
    *out_count = 0;

    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root))) {
        return false;
    }
    if (!tool_delegate_file_is_directory(repo_root)) {
        return false;
    }

    strscpy(paths[count++], repo_root, sizeof(paths[0]));

    snprintf(input_json, sizeof(input_json),
             "{\"action\":\"list\",\"path\":\"%s\"}",
             repo_root);
    if (tool_list_dir_execute(input_json, output, sizeof(output)) != 0) {
        *out_count = count;
        return count >= 3;
    }

    cursor = output;
    while (cursor && *cursor && child_count < (int)ARRAY_SIZE(children)) {
        char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (len > 0 && len < sizeof(children[0]) && cursor[0] == '/') {
            char line[160];
            const char *base = NULL;
            memcpy(line, cursor, len);
            line[len] = '\0';
            if (strcmp(line, repo_root) != 0 && tool_delegate_file_is_directory(line)) {
                base = tool_delegate_path_basename(line);
                if (base && base[0] && base[0] != '.' && strncmp(base, "build", 5) != 0) {
                    strscpy(children[child_count++], line, sizeof(children[0]));
                }
            }
        }
        cursor = line_end ? line_end + 1 : NULL;
    }

    for (int i = 0; i < child_count && count < 4; i++) {
        if (!path_already_selected(paths, count, children[i])) {
            strscpy(paths[count++], children[i], sizeof(paths[0]));
        }
    }

    *out_count = count;
    return count >= 3;
}

static bool resolve_batch_scopes(const delegate_request_t *req,
                                 char paths[][512],
                                 int *out_count)
{
    if (collect_explicit_prompt_paths(req, paths, out_count)) {
        return true;
    }
    return resolve_repo_root_auto_batch_scopes(req, paths, out_count);
}

bool tool_delegate_request_prefers_parallel_scope_batch(const delegate_request_t *req)
{
    char paths[4][512];
    int count = 0;
    bool is_explore;
    bool preserves_root;
    bool bounded_overview;
    bool has_explicit_scopes;

    if (!req) {
        return false;
    }

    is_explore = strcmp(req->subagent_type, "explore") == 0;
    if (!is_explore || req->is_batch || req->run_in_background) {
        return false;
    }

    has_explicit_scopes = collect_explicit_prompt_paths(req, paths, &count);
    bounded_overview = tool_delegate_request_is_bounded_explore_overview(req) || has_explicit_scopes;
    preserves_root = tool_delegate_overview_request_preserves_repo_root(req->prompt, req->description);

    if (!bounded_overview) {
        return false;
    }
    if (!preserves_root && !has_explicit_scopes) {
        return false;
    }
    return resolve_batch_scopes(req, paths, &count) && count >= 3;
}

bool tool_delegate_should_expand_parallel_scope_batch(const delegate_request_t *req)
{
    char paths[4][512];
    int count = 0;
    bool is_batch_request;
    bool is_background_request;
    bool has_target_path;
    bool target_path_blocks_batch;
    bool is_explore;
    bool bounded_overview;
    bool preserves_root;
    bool resolved_paths;
    bool has_explicit_scopes;

    if (!req) {
        return false;
    }
    if (tool_delegate_request_prefers_parallel_scope_batch(req)) {
        return true;
    }
    is_batch_request = req->is_batch;
    is_background_request = req->run_in_background;
    has_target_path = req->target_path[0] != '\0';
    target_path_blocks_batch = false;
    is_explore = strcmp(req->subagent_type, "explore") == 0;
    has_explicit_scopes = collect_explicit_prompt_paths(req, paths, &count);
    bounded_overview = tool_delegate_request_is_bounded_explore_overview(req) || has_explicit_scopes;
    preserves_root = tool_delegate_overview_request_preserves_repo_root(req->prompt, req->description);

    if (has_target_path) {
        target_path_blocks_batch = !resolve_batch_scopes(req, paths, &count);
    }

    if (is_batch_request || is_background_request || target_path_blocks_batch) {
        pr_info("delegate parallel-scope batch skip: desc=%s reason=batch=%d background=%d target_path=%d target_path_blocks=%d",
                req->description[0] ? req->description : "-",
                is_batch_request,
                is_background_request,
                has_target_path,
                target_path_blocks_batch);
        return false;
    }
    if (!is_explore) {
        pr_info("delegate parallel-scope batch skip: desc=%s reason=subagent_type=%s",
                req->description[0] ? req->description : "-",
                req->subagent_type[0] ? req->subagent_type : "-");
        return false;
    }
    if (!bounded_overview) {
        pr_info("delegate parallel-scope batch skip: desc=%s reason=not_bounded_overview",
                req->description[0] ? req->description : "-");
        return false;
    }
    if (!preserves_root && !has_explicit_scopes) {
        pr_info("delegate parallel-scope batch skip: desc=%s reason=root_not_preserved",
                req->description[0] ? req->description : "-");
        return false;
    }

    resolved_paths = has_target_path ? !target_path_blocks_batch
                                     : resolve_batch_scopes(req, paths, &count);
    if (!resolved_paths) {
        pr_info("delegate parallel-scope batch skip: desc=%s reason=scope_resolution_failed",
                req->description[0] ? req->description : "-");
        return false;
    }
    return true;
}

void tool_delegate_fill_parallel_scope_batch_request(const delegate_request_t *req,
                                                     delegate_request_t *batch_req)
{
    char paths[4][512];
    int count = 0;

    if (!req || !batch_req) {
        return;
    }

    memset(batch_req, 0, sizeof(*batch_req));
    if (!resolve_batch_scopes(req, paths, &count)) {
        return;
    }

    batch_req->is_batch = true;
    batch_req->batch_count = 0;

    for (int i = 1; i < count && batch_req->batch_count < DELEGATE_COORDINATOR_AGENTS_MAX; i++) {
        const char *path = paths[i];
        const char *subagent_type = infer_scope_subagent_type_from_user_prompt(
            req->prompt[0] ? req->prompt : req->description);
        int idx = batch_req->batch_count++;
        build_generic_scope_task_description(subagent_type,
                                             path,
                                             batch_req->batch_tasks[idx].description,
                                             sizeof(batch_req->batch_tasks[idx].description));
        build_scoped_subagent_prompt(req->prompt[0] ? req->prompt : req->description,
                                     paths[0],
                                     path,
                                     batch_req->batch_tasks[idx].prompt,
                                     sizeof(batch_req->batch_tasks[idx].prompt));
        strscpy(batch_req->batch_tasks[idx].subagent_type, subagent_type,
                sizeof(batch_req->batch_tasks[idx].subagent_type));
        strscpy(batch_req->batch_tasks[idx].target_path,
                path,
                sizeof(batch_req->batch_tasks[idx].target_path));
    }
}

char *tool_delegate_build_parallel_scope_batch_json(const delegate_request_t *req,
                                                    bool include_sudo_preflight)
{
    delegate_request_t batch_req;
    cJSON *root = NULL;
    cJSON *tasks = NULL;
    char *json = NULL;
    char repo_root[512];
    char task_keys[DELEGATE_COORDINATOR_AGENTS_MAX][64];
    bool add_oracle = false;

    if (!req) {
        return NULL;
    }
    memset(&batch_req, 0, sizeof(batch_req));
    memset(repo_root, 0, sizeof(repo_root));
    tool_delegate_fill_parallel_scope_batch_request(req, &batch_req);
    if (!batch_req.is_batch || batch_req.batch_count <= 0) {
        return NULL;
    }
    if (!tool_delegate_resolve_repo_root(req, repo_root, sizeof(repo_root)) || !repo_root[0]) {
        return NULL;
    }

    root = cJSON_CreateObject();
    tasks = cJSON_CreateArray();
    if (!root || !tasks) {
        cJSON_Delete(root);
        cJSON_Delete(tasks);
        return NULL;
    }

    for (int i = 0; i < batch_req.batch_count; i++) {
        cJSON *task = cJSON_CreateObject();
        char task_key[64];
        if (!task) {
            continue;
        }
        build_generic_scope_task_key(batch_req.batch_tasks[i].subagent_type,
                                     batch_req.batch_tasks[i].target_path,
                                     i,
                                     task_key,
                                     sizeof(task_key));
        cJSON_AddStringToObject(task, "task_key", task_key);
        cJSON_AddStringToObject(task, "description", batch_req.batch_tasks[i].description);
        cJSON_AddStringToObject(task, "subagent_type", batch_req.batch_tasks[i].subagent_type);
        cJSON_AddStringToObject(task, "target_path", batch_req.batch_tasks[i].target_path);
        cJSON_AddStringToObject(task, "prompt", batch_req.batch_tasks[i].prompt);
        cJSON_AddItemToArray(tasks, task);
        strscpy(task_keys[i], task_key, sizeof(task_keys[i]));
    }

    if (include_sudo_preflight) {
        cJSON *task = cJSON_CreateObject();
        cJSON *preflight = cJSON_CreateObject();
        cJSON *input = cJSON_CreateObject();

        if (task && preflight && input) {
            cJSON_AddStringToObject(task, "description", "验证 sudo 权限链路");
            cJSON_AddStringToObject(task, "subagent_type", "explore");
            cJSON_AddStringToObject(task, "target_path", repo_root);
            cJSON_AddStringToObject(task, "prompt",
                                    "验证 sudo 权限链路，并基于真实工具结果解释为什么会请求 sudo、如果用户取消会如何阻塞。不要假装执行，必须基于 preflight_tool 的真实输出总结。");
            cJSON_AddStringToObject(preflight, "tool_name", "terminal");
            cJSON_AddStringToObject(input, "command", "sudo ls /root");
            cJSON_AddStringToObject(input, "workdir", repo_root);
            cJSON_AddItemToObject(preflight, "input", input);
            cJSON_AddBoolToObject(preflight, "continue_on_error", false);
            cJSON_AddItemToObject(task, "preflight_tool", preflight);
            cJSON_AddItemToArray(tasks, task);
        } else {
            cJSON_Delete(task);
            cJSON_Delete(preflight);
            cJSON_Delete(input);
        }
    }

    add_oracle = prompt_looks_like_repo_synthesis_request(req->prompt[0] ? req->prompt : req->description);
    if (add_oracle && batch_req.batch_count >= 2) {
        cJSON *task = cJSON_CreateObject();
        cJSON *depends = cJSON_CreateArray();
        char oracle_prompt[4096];

        if (task && depends) {
            build_scope_oracle_prompt(req->prompt[0] ? req->prompt : req->description,
                                      repo_root,
                                      &batch_req,
                                      oracle_prompt,
                                      sizeof(oracle_prompt));
            cJSON_AddStringToObject(task, "task_key", "oracle_synthesis");
            cJSON_AddStringToObject(task, "description", "synthesize architecture findings");
            cJSON_AddStringToObject(task, "subagent_type", "oracle");
            cJSON_AddStringToObject(task, "target_path", repo_root);
            cJSON_AddStringToObject(task, "prompt", oracle_prompt);
            for (int i = 0; i < batch_req.batch_count; i++) {
                if (task_keys[i][0]) {
                    cJSON_AddItemToArray(depends, cJSON_CreateString(task_keys[i]));
                }
            }
            cJSON_AddItemToObject(task, "depends_on", depends);
            cJSON_AddItemToArray(tasks, task);
            cJSON_AddStringToObject(root, "dispatch_mode", "staged");
        } else {
            cJSON_Delete(task);
            cJSON_Delete(depends);
        }
    }
    if (!cJSON_GetObjectItem(root, "dispatch_mode")) {
        cJSON_AddStringToObject(root, "dispatch_mode", "parallel");
    }
    cJSON_AddItemToObject(root, "tasks", tasks);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
