/* 文件工具辅助层：路径解析、文本读取与精确替换。 */

#include "tools/tool_file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitize_snippet(char *text)
{
    if (!text) return;
    for (char *p = text; *p; ++p) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            *p = ' ';
        }
    }
}

int tool_files_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int tool_files_json_get_int_default(cJSON *obj, const char *key, int default_value)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

bool tool_files_json_get_bool_default(cJSON *obj, const char *key, bool default_value)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item) {
        return default_value;
    }
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    return default_value;
}

void tool_files_trim_line_end(char *line)
{
    if (!line) return;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

int tool_files_count_total_lines(FILE *f)
{
    if (!f) return 0;

    int total = 0;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) != -1) {
        total++;
    }
    free(line);
    rewind(f);
    return total;
}

daima_err_t tool_files_read_text_file(const char *path,
                                     size_t max_size,
                                     char **buf_out,
                                     size_t *len_out)
{
    if (!path || !buf_out || !len_out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return DAIMA_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0 || (max_size > 0 && (size_t)file_size > max_size)) {
        fclose(f);
        return DAIMA_ERR_INVALID_SIZE;
    }

    char *buf = malloc((size_t)file_size + 1);
    if (!buf) {
        fclose(f);
        return DAIMA_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    buf[n] = '\0';
    *buf_out = buf;
    *len_out = n;
    return DAIMA_OK;
}

daima_err_t tool_files_read_optional_text_file(const char *path,
                                              size_t max_size,
                                              char **buf_out,
                                              size_t *len_out)
{
    if (!buf_out || !len_out) {
        return DAIMA_ERR_INVALID_ARG;
    }
    *buf_out = NULL;
    *len_out = 0;

    daima_err_t err = tool_files_read_text_file(path, max_size, buf_out, len_out);
    if (err == DAIMA_ERR_NOT_FOUND || err == DAIMA_ERR_INVALID_SIZE) {
        *buf_out = calloc(1, 1);
        if (!*buf_out) {
            return DAIMA_ERR_NO_MEM;
        }
        *len_out = 0;
        return DAIMA_OK;
    }
    return err;
}

daima_err_t tool_files_write_text_file(const char *path,
                                      const char *content,
                                      size_t len)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        return DAIMA_FAIL;
    }

    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len ? DAIMA_OK : DAIMA_FAIL;
}

daima_err_t tool_files_apply_replace(const char *input,
                                    size_t input_len,
                                    const char *old_str,
                                    const char *new_str,
                                    bool replace_all,
                                    char **result_out,
                                    size_t *result_len_out,
                                    int *replaced_count_out,
                                    size_t *first_match_offset_out)
{
    if (!input || !old_str || !new_str || !result_out || !result_len_out || !replaced_count_out) {
        return DAIMA_ERR_INVALID_ARG;
    }
    if (!old_str[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    int match_count = 0;
    const char *scan_count = input;
    const char *pos_count = NULL;
    size_t first_match_offset = (size_t)-1;
    while ((pos_count = strstr(scan_count, old_str)) != NULL) {
        if (first_match_offset == (size_t)-1) {
            first_match_offset = (size_t)(pos_count - input);
        }
        match_count++;
        scan_count = pos_count + old_len;
        if (!replace_all) {
            break;
        }
    }
    if (match_count == 0) {
        return DAIMA_ERR_NOT_FOUND;
    }

    size_t max_result = input_len + 1;
    if (new_len > old_len) {
        max_result += (size_t)(new_len - old_len) * (size_t)match_count;
    }
    char *result = malloc(max_result);
    if (!result) {
        return DAIMA_ERR_NO_MEM;
    }

    size_t total = 0;
    const char *scan = input;
    const char *pos = NULL;
    int replaced = 0;
    while ((pos = strstr(scan, old_str)) != NULL) {
        size_t prefix_len = (size_t)(pos - scan);
        memcpy(result + total, scan, prefix_len);
        total += prefix_len;
        memcpy(result + total, new_str, new_len);
        total += new_len;
        scan = pos + old_len;
        replaced++;
        if (!replace_all) {
            break;
        }
    }

    size_t suffix_len = input_len - (size_t)(scan - input);
    memcpy(result + total, scan, suffix_len);
    total += suffix_len;
    result[total] = '\0';

    *result_out = result;
    *result_len_out = total;
    *replaced_count_out = replaced;
    if (first_match_offset_out) {
        *first_match_offset_out = first_match_offset;
    }
    return DAIMA_OK;
}

void tool_files_build_patch_preview_snippet(const char *text,
                                            size_t text_len,
                                            size_t match_offset,
                                            size_t token_len,
                                            char *out,
                                            size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!text || match_offset == (size_t)-1 || match_offset >= text_len) {
        return;
    }

    size_t start = match_offset > 40 ? match_offset - 40 : 0;
    size_t end = match_offset + token_len + 40;
    if (end > text_len) {
        end = text_len;
    }

    size_t off = 0;
    if (start > 0) {
        off += snprintf(out + off, out_size - off, "...");
    }

    size_t copy = end > start ? (end - start) : 0;
    if (copy > out_size - off - 1) {
        copy = out_size - off - 1;
    }
    memcpy(out + off, text + start, copy);
    off += copy;
    out[off] = '\0';

    if (end < text_len && off < out_size - 1) {
        snprintf(out + off, out_size - off, "...");
    }
    sanitize_snippet(out);
}
