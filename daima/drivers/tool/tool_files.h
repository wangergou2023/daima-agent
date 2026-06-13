/* 文件工具接口。 */

#pragma once

#include "core/err.h"
#include "drivers/tool/tool_registry.h"
#include <stddef.h>

#define TOOL_FILES_MAX_FILE_SIZE (32 * 1024)
#define TOOL_FILES_PATH_SIZE 1024
#define TOOL_FILES_READ_HEADER_RESERVE 256

/**
 * 统一文件查看工具。
 * 输入 JSON：{"action":"read","path":"./file.c","offset":1,"limit":120}
 *          {"action":"list","path":"./main","prefix":"./main/agent"}
 *          {"action":"search","pattern":"agent_loop","target":"content","path":"./main"}
 */
daima_err_t tool_files_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_files_definition(void);

/* Internal helpers reused by the files action dispatcher. */
daima_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * 应用 Codex 风格补丁。
 * - 支持 Add File / Update File / Delete File
 * - 输入 JSON：{"patch":"*** Begin Patch\n*** Add File: a.txt\n+hello\n*** End Patch\n"}
 */
daima_err_t tool_apply_patch_execute(const char *input_json, char *output, size_t output_size);
const daima_tool_t *tool_apply_patch_definition(void);

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
