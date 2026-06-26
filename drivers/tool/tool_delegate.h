/* delegate_task 工具接口 */
#pragma once
#include <stdbool.h>
#include "drivers/tool/tool_types.h"
const struct tool *tool_delegate_definition(void);
const struct tool_driver *tool_delegate_driver(void);
bool tool_delegate_text_has_dsml_markup(const char *text);
bool tool_delegate_text_has_transcript_markup_public(const char *text);
void tool_delegate_sanitize_summary_text_copy(char *dst, size_t dst_size, const char *src);
const char *tool_delegate_safe_output_text(const char *final_text,
                                           const char *reasoning_text,
                                           bool tool_budget_exhausted,
                                           bool cancelled);
bool tool_delegate_parse_result_json_summary(const char *text, char *summary, size_t summary_size);
bool tool_delegate_parse_result_json_rendered(const char *text, char *summary, size_t summary_size);
bool tool_delegate_finalize_result_json(const char *subagent_type,
                                        const char *description,
                                        const char *raw_text,
                                        char *summary,
                                        size_t summary_size);
bool tool_delegate_prepare_subagent_prompt(const char *subagent_type,
                                           const char *description,
                                           const char *prompt,
                                           char *prepared_prompt,
                                           size_t prepared_prompt_size,
                                           bool *disable_tools);
