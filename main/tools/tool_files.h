/* 文件工具接口。 */

#pragma once

#include "daima_err.h"
#include "tools/tool_registry.h"
#include <stddef.h>

#define TOOL_FILES_MAX_FILE_SIZE (32 * 1024)
#define TOOL_FILES_PATH_SIZE 1024
#define TOOL_FILES_READ_HEADER_RESERVE 256

/**
 * 读取文本文件。
 * - 支持当前工作目录下的相对路径、绝对路径，以及 SPIFFS 文件
 * - 按行分页输出，带行号
 * 输入 JSON：{"path":"./file.c","offset":1,"limit":120}
 */
daima_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_read_file_definition(void);

/**
 * 写入/覆盖文本文件。
 * - 支持当前工作目录相对路径、工作区内绝对路径，以及 SPIFFS 文件
 * 输入 JSON：{"path":"./notes.md","content":"..."}
 */
daima_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_write_file_definition(void);

/**
 * 对文本文件执行查找并替换。
 * - 支持当前工作目录相对路径、工作区内绝对路径，以及 SPIFFS 文件
 * - replace_all=true 时替换全部匹配
 * 输入 JSON：{"path":"./main.c","old_string":"...","new_string":"...","replace_all":true}
 */
daima_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_edit_file_definition(void);

/**
 * 对同一个文本文件执行多步精确补丁。
 * - 支持当前工作目录相对路径、工作区内绝对路径，以及 SPIFFS 文件
 * - 适合一次修改多个位置，所有 edits 都成功后才写回
 * - preview=true 时只预览，不写回文件
 * 输入 JSON：{"path":"./main.c","preview":true,"edits":[{"old_string":"a","new_string":"b"},{"old_string":"x","new_string":"y","replace_all":true}]}
 */
daima_err_t tool_patch_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_patch_definition(void);

/**
 * 将文件恢复到最近一次检查点，或恢复到指定 checkpoint_path。
 * - 默认按 path 查最近检查点
 * - 也可显式传 checkpoint_path
 * 输入 JSON：{"path":"./main.c"} 或 {"path":"./main.c","checkpoint_path":"./spiffs_data/cache/checkpoints/...bak"}
 */
daima_err_t tool_restore_file_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_restore_file_definition(void);

/**
 * 列出目录文件。
 * - 默认列出当前工作目录，也支持 SPIFFS 目录
 * 输入 JSON：{"path":"./main","prefix":"./main/agent"}（均可选）
 */
daima_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_list_dir_definition(void);

/**
 * 搜索文件名或文件内容。
 * - 支持当前工作目录和 SPIFFS 目录
 * - target=content 时按子串搜索文本内容；target=files 时按文件名搜索
 * - output_mode 支持 content / files_only / count
 * - context 可返回匹配行前后的少量上下文
 * - offset 可用于分页
 * 输入 JSON：{"pattern":"agent_loop","target":"content","path":"./main","output_mode":"content","context":2,"limit":20,"offset":0}
 */
daima_err_t tool_search_files_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_search_files_definition(void);
