/* 飞书消息内容解析辅助。 */

#pragma once

#include <stddef.h>

typedef struct cJSON cJSON;

void feishu_normalize_text(const char *src, char *dst, size_t dst_size);
void feishu_collect_post_parts(cJSON *node, char *text_buf, size_t text_buf_size, const char **image_key_out);
void feishu_build_image_prompt(const char *user_text, char *dst, size_t dst_size);
