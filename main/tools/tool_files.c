/* 文件工具定义。 */

#include "tools/tool_files.h"

#include "daima_config.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static const daima_tool_t s_files_tool = {
    .name = "files",
    .description = "统一文件查看工具。action=read 分页读取文本文件；action=list 列目录；action=search 搜索文件名或文本内容。文件修改只使用 apply_patch。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"read、list 或 search\"},"
        "\"path\":{\"type\":\"string\",\"description\":\"文件或目录路径，支持相对路径、绝对路径，或数据目录快捷前缀 spiffs_data/...\"},"
        "\"prefix\":{\"type\":\"string\",\"description\":\"list 时可选的完整路径前缀过滤\"},"
        "\"pattern\":{\"type\":\"string\",\"description\":\"search 时要搜索的关键词\"},"
        "\"target\":{\"type\":\"string\",\"description\":\"search 时为 content 或 files，默认 content\"},"
        "\"file_glob\":{\"type\":\"string\",\"description\":\"search 时可选文件名通配，例如 *.c 或 *.md\"},"
        "\"output_mode\":{\"type\":\"string\",\"description\":\"search 结果展示方式：content / files_only / count\"},"
        "\"context\":{\"type\":\"integer\",\"description\":\"search content 时返回匹配行前后多少行上下文\"},"
        "\"offset\":{\"type\":\"integer\",\"description\":\"起始行号（从 1 开始，可选）\"},"
        "\"limit\":{\"type\":\"integer\",\"description\":\"最多读取多少行或返回多少条结果（可选）\"}"
        "},"
        "\"required\":[\"action\"]}",
    .execute = tool_files_execute,
};

static const daima_tool_t s_apply_patch_tool = {
    .name = "apply_patch",
    .description = "应用 Codex 风格文本补丁，是新建、修改、删除代码或文本文件的唯一文件修改工具。patch 字符串必须以 *** Begin Patch 开始、以 *** End Patch 结束，支持 *** Add File、*** Update File、*** Delete File。修改前先用 files action=read/search 看上下文。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"patch\":{\"type\":\"string\",\"description\":\"Codex 风格补丁文本，例如 *** Begin Patch\\n*** Update File: path\\n@@\\n-old\\n+new\\n*** End Patch\\n\"}},"
        "\"required\":[\"patch\"]}",
    .execute = tool_apply_patch_execute,
};

static const daima_tool_t s_restore_file_tool = {
    .name = "restore_file",
    .description = "将文件恢复到最近一次检查点，或恢复到指定 checkpoint_path。适合在 apply_patch 后验证失败时快速回退。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"要恢复的文件路径\"},"
        "\"checkpoint_path\":{\"type\":\"string\",\"description\":\"可选：指定检查点文件路径；不传则自动使用最近一次检查点\"}},"
        "\"required\":[\"path\"]}",
    .execute = tool_restore_file_execute,
};

daima_err_t tool_files_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    if (!action || !action[0]) {
        snprintf(output, output_size, "错误：缺少 action 字段（read/list/search）");
        cJSON_Delete(root);
        return DAIMA_ERR_INVALID_ARG;
    }

    daima_err_t err;
    if (strcmp(action, "read") == 0) {
        err = tool_read_file_execute(input_json, output, output_size);
    } else if (strcmp(action, "list") == 0) {
        err = tool_list_dir_execute(input_json, output, output_size);
    } else if (strcmp(action, "search") == 0) {
        err = tool_search_files_execute(input_json, output, output_size);
    } else {
        snprintf(output, output_size, "错误：action 必须是 read、list 或 search");
        err = DAIMA_ERR_INVALID_ARG;
    }

    cJSON_Delete(root);
    return err;
}

const daima_tool_t *tool_files_definition(void)
{
    return &s_files_tool;
}

const daima_tool_t *tool_apply_patch_definition(void)
{
    return &s_apply_patch_tool;
}

const daima_tool_t *tool_restore_file_definition(void)
{
    return &s_restore_file_tool;
}
