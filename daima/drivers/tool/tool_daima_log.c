#include "drivers/tool/tool_daima_log.h"

#include "paths.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LINE_MAX 2048

static const struct tool s_daima_log_tool = {
    .name = "daima_log",
    .description = "读取 daima 自身运行日志，用于诊断工具失败、网络错误、系统异常。支持 tail / search / errors。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"action\":{\"type\":\"string\",\"description\":\"tail / search / errors，默认 tail\"},"
        "\"pattern\":{\"type\":\"string\",\"description\":\"search 时使用的搜索关键词（大小写不敏感）\"},"
        "\"lines\":{\"type\":\"integer\",\"description\":\"输出最近多少行，默认 50，最大 200\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_daima_log_execute,
};

static int input_lines(const cJSON *input)
{
    cJSON *n = cJSON_GetObjectItem((cJSON *)input, "lines");
    if (!n || !cJSON_IsNumber(n) || n->valueint <= 0) return 50;
    if (n->valueint > 200) return 200;
    return n->valueint;
}

static size_t count_lines(FILE *f)
{
    size_t count = 0;
    int c;
    long pos = ftell(f);
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') count++;
    }
    fseek(f, pos, SEEK_SET);
    return count;
}

static bool line_contains_icase(const char *line, const char *pattern)
{
    if (!pattern || !pattern[0]) return true;
    size_t plen = strlen(pattern);
    for (const char *p = line; *p; p++) {
        size_t j;
        for (j = 0; j < plen && p[j]; j++) {
            if (tolower((unsigned char)p[j]) != tolower((unsigned char)pattern[j])) break;
        }
        if (j == plen) return true;
    }
    return false;
}

static bool line_is_error(const char *line)
{
    return strstr(line, " [E] ") != NULL || strstr(line, " [W] ") != NULL;
}

static void sanitize_utf8(char *buf)
{
    unsigned char *p = (unsigned char *)buf;
    size_t wi = 0;
    for (size_t i = 0; buf[i]; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x80) { buf[wi++] = (char)c; continue; }
        if (c < 0xC0) { buf[wi++] = '?'; continue; }
        int n = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : (c < 0xF8) ? 4 : 1;
        bool valid = true;
        for (int j = 1; j < n; j++) {
            if (!buf[i + j] || ((unsigned char)buf[i + j] & 0xC0) != 0x80) { valid = false; break; }
        }
        if (valid) {
            for (int j = 0; j < n; j++) buf[wi++] = buf[i + j];
            i += n - 1;
        } else {
            buf[wi++] = '?';
        }
    }
    buf[wi] = '\0';
}

daima_err_t tool_daima_log_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *input = cJSON_Parse(input_json ? input_json : "{}");
    if (!input || !cJSON_IsObject(input)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(input);
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(input, "action"));
    if (!action || !action[0]) action = "tail";

    const char *path = daima_path_log_file();
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "（日志文件为空或不存在）");
        cJSON_Delete(input);
        return DAIMA_OK;
    }

    int n = input_lines(input);
    size_t off = 0;

    if (strcmp(action, "tail") == 0) {
        size_t total = count_lines(f);
        int skip = (int)total > n ? (int)(total - n) : 0;
        char line[LOG_LINE_MAX];
        int line_no = 0;
        while (fgets(line, sizeof(line), f) && off < output_size - 1) {
            if (line_no++ < skip) continue;
            size_t len = strlen(line);
            if (off + len >= output_size - 1) break;
            memcpy(output + off, line, len);
            off += len;
        }
    } else if (strcmp(action, "search") == 0) {
        const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(input, "pattern"));
        if (!pattern || !pattern[0]) {
            snprintf(output, output_size, "错误：search 需要 pattern 参数");
            fclose(f);
            cJSON_Delete(input);
            return DAIMA_ERR_INVALID_ARG;
        }
        char line[LOG_LINE_MAX];
        int shown = 0;
        while (fgets(line, sizeof(line), f) && off < output_size - 1 && shown < n) {
            if (!line_contains_icase(line, pattern)) continue;
            size_t len = strlen(line);
            if (off + len >= output_size - 1) break;
            memcpy(output + off, line, len);
            off += len;
            shown++;
        }
    } else if (strcmp(action, "errors") == 0) {
        char line[LOG_LINE_MAX];
        int shown = 0;
        while (fgets(line, sizeof(line), f) && off < output_size - 1 && shown < n) {
            if (!line_is_error(line)) continue;
            size_t len = strlen(line);
            if (off + len >= output_size - 1) break;
            memcpy(output + off, line, len);
            off += len;
            shown++;
        }
    } else {
        snprintf(output, output_size, "错误：未知 action=%s，支持 tail / search / errors", action);
        fclose(f);
        cJSON_Delete(input);
        return DAIMA_ERR_INVALID_ARG;
    }

    if (off == 0) {
        snprintf(output, output_size, "（无匹配内容）");
    } else {
        output[off] = '\0';
        sanitize_utf8(output);
    }

    fclose(f);
    cJSON_Delete(input);
    return DAIMA_OK;
}

const struct tool *tool_daima_log_definition(void)
{
    return &s_daima_log_tool;
}
