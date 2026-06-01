/* 文件工具定义。 */

#include "tools/tool_files.h"

#include "daima_config.h"

static const daima_tool_t s_read_file_tool = {
    .name = "read_file",
    .description = "读取文本文件，支持当前工作目录下的相对路径、绝对路径，以及数据目录下的文件。数据目录既可用绝对路径，也可用 spiffs_data/... 作为快捷前缀。返回带行号的分页文本；大文件请使用 offset/limit。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"文件路径，支持相对路径、绝对路径，或数据目录快捷前缀 spiffs_data/...\"},"
        "\"offset\":{\"type\":\"integer\",\"description\":\"起始行号（从 1 开始，可选）\"},"
        "\"limit\":{\"type\":\"integer\",\"description\":\"最多读取多少行（可选）\"}"
        "},"
        "\"required\":[\"path\"]}",
    .execute = tool_read_file_execute,
};

static const daima_tool_t s_write_file_tool = {
    .name = "write_file",
    .description = "写入文本文件并覆盖整个文件内容，支持当前工作目录下的相对路径、工作区内绝对路径，以及数据目录下路径。数据目录既可用绝对路径，也可用 spiffs_data/... 作为快捷前缀。会自动创建父目录。更适合新建文件或用户明确要求整文件重写；修改已有文件时优先使用 patch 或 edit_file。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"文件路径：支持当前工作目录相对路径、工作区内绝对路径，或数据目录快捷前缀 spiffs_data/...\"},"
        "\"content\":{\"type\":\"string\",\"description\":\"要写入的完整文件内容；会覆盖原文件全部内容\"}},"
        "\"required\":[\"path\",\"content\"]}",
    .execute = tool_write_file_execute,
};

static const daima_tool_t s_edit_file_tool = {
    .name = "edit_file",
    .description = "在文本文件中查找并替换 old_string。默认替换首次出现；传 replace_all=true 时替换全部。适合小范围定点修改。支持当前工作目录下的相对路径、工作区内绝对路径，以及数据目录下路径；数据目录也可用 spiffs_data/... 快捷前缀。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"文件路径：支持当前工作目录相对路径、工作区内绝对路径，或数据目录快捷前缀 spiffs_data/...\"},"
        "\"old_string\":{\"type\":\"string\",\"description\":\"要查找的文本\"},"
        "\"new_string\":{\"type\":\"string\",\"description\":\"替换为的文本\"},"
        "\"replace_all\":{\"type\":\"boolean\",\"description\":\"是否替换全部匹配，默认 false\"}},"
        "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
    .execute = tool_edit_file_execute,
};

static const daima_tool_t s_patch_tool = {
    .name = "patch",
    .description = "对同一个文本文件执行多步精确替换。适合一次改多个位置，也是修改已有代码文件时的首选工具；所有 edits 都成功后才写回。preview=true 时只返回修改预览，不落盘。支持当前工作目录下的相对路径、工作区内绝对路径，以及数据目录下路径；数据目录也可用 spiffs_data/... 快捷前缀。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"文件路径：支持当前工作目录相对路径、工作区内绝对路径，或数据目录快捷前缀 spiffs_data/...\"},"
        "\"preview\":{\"type\":\"boolean\",\"description\":\"是否只预览 patch 结果而不写回文件，默认 false\"},"
        "\"edits\":{\"type\":\"array\",\"description\":\"按顺序应用的精确替换列表\",\"items\":{\"type\":\"object\",\"properties\":{\"old_string\":{\"type\":\"string\",\"description\":\"要查找的原文本\"},\"new_string\":{\"type\":\"string\",\"description\":\"替换后的新文本\"},\"replace_all\":{\"type\":\"boolean\",\"description\":\"该条 edit 是否替换全部匹配，默认 false\"}},\"required\":[\"old_string\",\"new_string\"]}}},"
        "\"required\":[\"path\",\"edits\"]}",
    .execute = tool_patch_execute,
};

static const daima_tool_t s_restore_file_tool = {
    .name = "restore_file",
    .description = "将文件恢复到最近一次检查点，或恢复到指定 checkpoint_path。适合在 patch/edit/write 后验证失败时快速回退。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"要恢复的文件路径\"},"
        "\"checkpoint_path\":{\"type\":\"string\",\"description\":\"可选：指定检查点文件路径；不传则自动使用最近一次检查点\"}},"
        "\"required\":[\"path\"]}",
    .execute = tool_restore_file_execute,
};

static const daima_tool_t s_list_dir_tool = {
    .name = "list_dir",
    .description = "列出目录中的文件。默认列出当前工作目录，也支持数据目录下目录；数据目录也可用 spiffs_data/... 快捷前缀。可选 prefix 过滤。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"要列出的目录，默认当前工作目录；也可传数据目录绝对路径或 spiffs_data/...\"},"
        "\"prefix\":{\"type\":\"string\",\"description\":\"可选的完整路径前缀过滤，例如 spiffs_data/memory/\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_list_dir_execute,
};

static const daima_tool_t s_search_files_tool = {
    .name = "search_files",
    .description = "搜索文件名或文本内容。默认在当前工作目录搜索，也支持数据目录下目录；数据目录也可用 spiffs_data/... 快捷前缀。target=content 时按子串搜索文本内容；target=files 时按文件名搜索。output_mode 可选 content / files_only / count；content 模式还支持 context 返回上下文行。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pattern\":{\"type\":\"string\",\"description\":\"要搜索的关键词；content 模式搜索文本子串，files 模式搜索文件名\"},"
        "\"target\":{\"type\":\"string\",\"description\":\"content 或 files，默认 content\"},"
        "\"path\":{\"type\":\"string\",\"description\":\"搜索起点目录，默认当前工作目录；也可传数据目录绝对路径或 spiffs_data/...\"},"
        "\"file_glob\":{\"type\":\"string\",\"description\":\"可选文件名通配，例如 *.c 或 *.md\"},"
        "\"output_mode\":{\"type\":\"string\",\"description\":\"结果展示方式：content / files_only / count（可选）\"},"
        "\"context\":{\"type\":\"integer\",\"description\":\"仅 target=content 时生效；返回匹配行前后多少行上下文，建议 0-3\"},"
        "\"limit\":{\"type\":\"integer\",\"description\":\"最多返回多少条结果（可选）\"},"
        "\"offset\":{\"type\":\"integer\",\"description\":\"跳过前 N 条结果，用于分页（可选）\"}"
        "},"
        "\"required\":[\"pattern\"]}",
    .execute = tool_search_files_execute,
};

const daima_tool_t *tool_read_file_definition(void)
{
    return &s_read_file_tool;
}

const daima_tool_t *tool_write_file_definition(void)
{
    return &s_write_file_tool;
}

const daima_tool_t *tool_edit_file_definition(void)
{
    return &s_edit_file_tool;
}

const daima_tool_t *tool_patch_definition(void)
{
    return &s_patch_tool;
}

const daima_tool_t *tool_restore_file_definition(void)
{
    return &s_restore_file_tool;
}

const daima_tool_t *tool_list_dir_definition(void)
{
    return &s_list_dir_tool;
}

const daima_tool_t *tool_search_files_definition(void)
{
    return &s_search_files_tool;
}
