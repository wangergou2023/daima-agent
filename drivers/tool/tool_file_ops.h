/* 文件工具辅助层：路径解析、文本读取与精确替换。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "err.h"
#include "cjson.h"

int tool_files_clamp_int(int value, int min_value, int max_value);
int tool_files_json_get_int_default(cJSON *obj, const char *key, int default_value);

void tool_files_trim_line_end(char *line);
int tool_files_count_total_lines(FILE *f);

bool tool_files_resolve_read_path(const char *path, char *resolved, size_t resolved_size);
bool tool_files_resolve_write_path(const char *path, char *resolved, size_t resolved_size);
bool tool_files_resolve_list_dir_path(const char *path, char *resolved, size_t resolved_size);

void tool_files_ensure_parent_dirs(const char *path);

err_t tool_files_read_text_file(const char *path,
                                     size_t max_size,
                                     char **buf_out,
                                     size_t *len_out);

err_t tool_files_read_optional_text_file(const char *path,
                                              size_t max_size,
                                              char **buf_out,
                                              size_t *len_out);

err_t tool_files_write_text_file(const char *path,
                                      const char *content,
                                      size_t len);

err_t tool_files_checkpoint_before_write(const char *path,
                                              const char *previous_content,
                                              size_t previous_len,
                                              char *checkpoint_path,
                                              size_t checkpoint_path_size);

bool tool_files_get_recent_checkpoint(const char *path,
                                      char *checkpoint_path,
                                      size_t checkpoint_path_size);

err_t tool_files_checkpoint_current_file(const char *path,
                                              size_t max_size,
                                              char *checkpoint_path,
                                              size_t checkpoint_path_size);

err_t tool_files_restore_checkpoint(const char *target_path,
                                         const char *checkpoint_path,
                                         size_t max_size,
                                         char *rollback_checkpoint_path,
                                         size_t rollback_checkpoint_path_size);

err_t tool_files_apply_replace(const char *input,
                                    size_t input_len,
                                    const char *old_str,
                                    const char *new_str,
                                    bool replace_all,
                                    char **result_out,
                                    size_t *result_len_out,
                                    int *replaced_count_out,
                                    size_t *first_match_offset_out);
