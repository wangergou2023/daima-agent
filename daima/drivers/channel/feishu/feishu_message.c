/* 飞书消息文本解析与图片提示组装。 */

#include "drivers/channel/feishu/feishu_message.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "linux/kernel.h"

static void trim_ascii_in_place(char *s)
{
    if (!s) return;

    char *start = s;
    while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t') start++;
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') break;
        s[--len] = '\0';
    }
}

void feishu_normalize_text(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    strscpy(dst, src, dst_size);
    trim_ascii_in_place(dst);

    if (strncmp(dst, "@_user_1 ", 9) == 0) {
        memmove(dst, dst + 9, strlen(dst + 9) + 1);
        trim_ascii_in_place(dst);
    }
}

static void append_text_line(char *dst, size_t dst_size, const char *text)
{
    if (!dst || dst_size == 0 || !text || !text[0]) return;

    char normalized[512];
    feishu_normalize_text(text, normalized, sizeof(normalized));
    if (!normalized[0]) return;

    size_t len = strlen(dst);
    if (len > 0 && len < dst_size - 1) {
        snprintf(dst + len, dst_size - len, "\n");
        len = strlen(dst);
    }
    if (len < dst_size - 1) {
        strscpy(dst + len, normalized, dst_size - len);
    }
}

void feishu_collect_post_parts(cJSON *node, char *text_buf, size_t text_buf_size, const char **image_key_out)
{
    if (!node) return;

    if (cJSON_IsArray(node)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, node) {
            feishu_collect_post_parts(item, text_buf, text_buf_size, image_key_out);
        }
        return;
    }

    if (!cJSON_IsObject(node)) {
        return;
    }

    cJSON *child = NULL;
    cJSON_ArrayForEach(child, node) {
        const char *key = child->string ? child->string : "";
        if (cJSON_IsString(child)) {
            if (!*image_key_out &&
                strcmp(key, "image_key") == 0 &&
                child->valuestring &&
                child->valuestring[0]) {
                *image_key_out = child->valuestring;
                continue;
            }
            if (strcmp(key, "text") == 0 ||
                strcmp(key, "title") == 0 ||
                strcmp(key, "content") == 0) {
                append_text_line(text_buf, text_buf_size, child->valuestring);
            }
            continue;
        }

        if (cJSON_IsObject(child) || cJSON_IsArray(child)) {
            feishu_collect_post_parts(child, text_buf, text_buf_size, image_key_out);
        }
    }
}

void feishu_build_image_prompt(const char *user_text, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';

    char normalized[512];
    feishu_normalize_text(user_text, normalized, sizeof(normalized));
    if (normalized[0]) {
        snprintf(dst, dst_size,
                 "请结合这张图片和用户的补充说明一起分析。用户补充：%s",
                 normalized);
        return;
    }

    snprintf(dst, dst_size, "请描述并分析这张图片。");
}
