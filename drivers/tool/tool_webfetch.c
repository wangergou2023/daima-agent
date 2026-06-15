#include "drivers/tool/tool_webfetch.h"

#include "cjson.h"
#include "autoconf.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "http.h"
#include "work_item.h"

#include <ctype.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux/slab.h"

#define WEBFETCH_TIMEOUT_MS  TIMEOUT_DEFAULT
#define WEBFETCH_MAX_BODY    (512 * 1024)

static void strip_html_remove_tags(char *buf, size_t max_body);
static void strip_html_collapse_whitespace(char *buf);

static const struct tool s_webfetch_tool = {
    .name = "webfetch",
    .description = "从指定 URL 获取网页内容，支持返回纯文本或原始 HTML。用于搜索信息、阅读文档、查阅 API 参考等。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"url\":{\"type\":\"string\",\"description\":\"要获取的完整 URL（必须 http:// 或 https://）\"},"
        "\"format\":{\"type\":\"string\",\"description\":\"返回格式：text（纯文本，默认）或 html（原始 HTML）\"}"
        "},"
        "\"required\":[\"url\"]}",
    .execute = tool_webfetch_execute,
};

static bool is_safe_url(const char *url)
{
    if (!url) return false;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return false;

    const char *host_start = strstr(url, "://");
    if (!host_start) return false;
    host_start += 3;
    const char *host_end = host_start;
    while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '@') host_end++;

    size_t host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len > 253) return false;

    char host[256];
    snprintf(host, sizeof(host), "%.*s", (int)host_len, host_start);

    if (strcmp(host, "localhost") == 0 || strncmp(host, "127.", 4) == 0) return false;
    if (strncmp(host, "10.", 3) == 0 || strncmp(host, "172.16.", 7) == 0) return false;
    if (strncmp(host, "192.168.", 8) == 0 || strncmp(host, "169.254.", 8) == 0) return false;
    if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "[::1]") == 0) return false;

    return true;
}

static void clean_url(char *buf, size_t buf_size)
{
    if (!buf || !buf[0]) return;

    char *start = buf;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    char *end = start + strlen(start);
    while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t' ||
           *(end - 1) == '\n' || *(end - 1) == '\r')) end--;

    if (end > start + 2 && *start == '"' && *(end - 1) == '"') { start++; end--; }
    if (end > start + 2 && *start == '\'' && *(end - 1) == '\'') { start++; end--; }

    if (*start == '<' && *(end - 1) == '>') { start++; end--; }

    const char *paren = strrchr(start, ')');
    const char *last_paren = paren;
    if (last_paren && last_paren == end - 1) {
        const char *open = strchr(start, '(');
        if (open && open < last_paren) {
            start = (char *)open + 1;
            end = (char *)last_paren;
        }
    }

    const char *scheme = strstr(start, "http://");
    if (!scheme) scheme = strstr(start, "https://");
    if (scheme && scheme != start) start = (char *)scheme;

    size_t new_len = (size_t)(end - start);
    if (new_len >= buf_size) new_len = buf_size - 1;
    if (start != buf) memmove(buf, start, new_len);
    buf[new_len] = '\0';
}

static void html_decode(char *dst, const char *src, size_t dst_size)
{
    static const struct { const char *entity; char ch; } entities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'},
        {"&#39;", '\''}, {"&apos;", '\''}, {"&nbsp;", ' '},
        {"&#x27;", '\''}, {"&#x2F;", '/'},
    };
    size_t di = 0;
    while (*src && di + 1 < dst_size) {
        if (*src == '&') {
            bool matched = false;
            for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
                size_t elen = strlen(entities[i].entity);
                if (strncmp(src, entities[i].entity, elen) == 0) {
                    dst[di++] = entities[i].ch;
                    src += elen;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            if (src[1] == '#' && isdigit((unsigned char)src[2])) {
                src++;
                int codepoint = 0;
                const char *p = src;
                while (*p >= '0' && *p <= '9') { codepoint = codepoint * 10 + (*p - '0'); p++; }
                if (*p == ';' && codepoint > 0 && codepoint < 256) {
                    dst[di++] = (char)codepoint;
                    src = p + 1;
                    continue;
                }
                src--;
            }
        }
        dst[di++] = *src++;
    }
    dst[di] = '\0';
}

static void sanitize_utf8(char *buf)
{
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

static void webfetch_report_error(const char *url, const char *error, char *output, size_t output_size)
{
    char title[BUF_SMALL];
    snprintf(title, sizeof(title), "webfetch 请求失败: %.200s", url);
    work_item_store_collect("defect", "test", title, "webfetch 工具 HTTP 请求超时或网络错误");
    if (error && error[0]) {
        snprintf(output, output_size, "错误：%s", error);
    } else {
        snprintf(output, output_size, "错误：HTTP 请求失败（超时或网络错误）");
    }
}

static void webfetch_report_http_error(const char *url, long status, char *output, size_t output_size)
{
    char title[BUF_SMALL];
    char desc[BUF_MEDIUM];
    snprintf(title, sizeof(title), "webfetch 返回异常状态码: %.200s", url);
    snprintf(desc, sizeof(desc), "webfetch 访问 %.200s 返回 HTTP %ld", url, status);
    work_item_store_collect("defect", "test", title, desc);
    snprintf(output, output_size, "错误：HTTP %ld", status);
}

static err_t webfetch_format_output(const char *body, size_t body_len, const char *format,
                                          char *output, size_t output_size)
{
    size_t len = body_len;
    if (len > WEBFETCH_MAX_BODY) len = WEBFETCH_MAX_BODY;

    if (strcmp(format, "text") == 0) {
        char *buf = kmalloc(len + 1, GFP_KERNEL);
        if (!buf) {
            snprintf(output, output_size, "错误：内存不足");
            return ERR_NO_MEM;
        }
        memcpy(buf, body, len);
        buf[len] = '\0';
        strip_html_remove_tags(buf, WEBFETCH_MAX_BODY);
        strip_html_collapse_whitespace(buf);
        html_decode(buf, buf, len + 1);

        size_t out_len = strlen(buf);
        if (out_len > output_size - 1) out_len = output_size - 1;
        memcpy(output, buf, out_len);
        output[out_len] = '\0';
        kfree(buf);
    } else {
        size_t out_len = len;
        if (out_len > output_size - 1) out_len = output_size - 1;
        memcpy(output, body, out_len);
        output[out_len] = '\0';
    }

    sanitize_utf8(output);
    return 0;
}

static bool tag_in_set(const char *tag, const char *const *arr, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(tag, arr[i]) == 0) return true;
    }
    return false;
}

static void strip_html_remove_tags(char *buf, size_t max_body)
{
    if (!buf || !buf[0]) return;

    static const char *skip_tags[] = {
        "script", "style", "noscript", "iframe", "object", "embed", "svg", "canvas",
        "nav", "header", "footer", "aside", "template",
    };
    static const char *block_open[] = {
        "p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr", "section", "article",
    };
    static const char *block_close[] = {
        "/p", "/div", "/li", "/h1", "/h2", "/h3", "/h4", "/h5", "/h6",
        "/tr", "/section", "/article", "/ul", "/ol", "/table", "/blockquote",
        "/pre",
    };
    static const char *newline_self[] = { "br", "br/", "hr", "hr/" };

    bool in_tag = false;
    int skip_depth = 0;
    size_t wi = 0;
    char lower_tag[32];
    int ti = 0;

    for (size_t i = 0; buf[i] && wi < max_body; i++) {
        char c = buf[i];
        if (c == '<') {
            in_tag = true;
            ti = 0;
            memset(lower_tag, 0, sizeof(lower_tag));
            continue;
        }
        if (in_tag) {
            if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (c == '>') {
                    if (tag_in_set(lower_tag, skip_tags, ARRAY_SIZE(skip_tags))) skip_depth++;
                    else if (lower_tag[0] == '/' && tag_in_set(lower_tag + 1, skip_tags, ARRAY_SIZE(skip_tags))) skip_depth--;
                    else if (skip_depth == 0) {
                        if (tag_in_set(lower_tag, newline_self, ARRAY_SIZE(newline_self))) buf[wi++] = '\n';
                        else if (tag_in_set(lower_tag, block_close, ARRAY_SIZE(block_close))) buf[wi++] = '\n';
                        else if (tag_in_set(lower_tag, block_open, ARRAY_SIZE(block_open))) {
                            if (wi > 0 && buf[wi-1] != '\n') buf[wi++] = '\n';
                        }
                    }
                }
                in_tag = false;
                if (c != '>') {
                    while (buf[i] && buf[i] != '>') i++;
                }
                continue;
            }
            if (ti < (int)(sizeof(lower_tag) - 1)) {
                lower_tag[ti++] = (char)tolower((unsigned char)c);
                lower_tag[ti] = '\0';
            }
            continue;
        }
        if (skip_depth > 0) continue;
        if (c == '\r') { buf[wi++] = '\n'; continue; }
        if (c == '\n' || c == '\t') { buf[wi++] = ' '; continue; }
        buf[wi++] = c;
    }
    buf[wi] = '\0';
}

static void strip_html_collapse_whitespace(char *buf)
{
    char *r = buf, *w = buf;
    while (*r) {
        if (*r == ' ' || *r == '\t') {
            if (w > buf && *(w - 1) != ' ' && *(w - 1) != '\n') *w++ = ' ';
            r++;
            continue;
        }
        if (*r == '\n') {
            while (r[1] == '\n' || r[1] == ' ' || r[1] == '\t') r++;
            if (w > buf && *(w - 1) != '\n') *w++ = '\n';
            r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = '\0';

    while (w > buf && (*(w - 1) == ' ' || *(w - 1) == '\n' || *(w - 1) == '\t')) { w--; *w = '\0'; }
    while (*buf == ' ' || *buf == '\n' || *buf == '\t') memmove(buf, buf + 1, strlen(buf));
}

err_t tool_webfetch_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *input = cJSON_Parse(input_json ? input_json : "{}");
    if (!input || !cJSON_IsObject(input)) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        cJSON_Delete(input);
        return ERR_INVALID_ARG;
    }

    const char *raw_url = cJSON_GetStringValue(cJSON_GetObjectItem(input, "url"));
    if (!raw_url || !raw_url[0]) {
        snprintf(output, output_size, "错误：缺少 url 参数");
        cJSON_Delete(input);
        return ERR_INVALID_ARG;
    }

    char url[BUF_MEDIUM * 4];
    strscpy(url, raw_url, sizeof(url));
    clean_url(url, sizeof(url));

    if (!is_safe_url(url)) {
        snprintf(output, output_size, "错误：URL 不允许（仅支持 http/https 公网地址）");
        cJSON_Delete(input);
        return ERR_INVALID_ARG;
    }

    const char *fmt_raw = cJSON_GetStringValue(cJSON_GetObjectItem(input, "format"));
    char format[8];
    if (fmt_raw && fmt_raw[0]) strscpy(format, fmt_raw, sizeof(format));
    else snprintf(format, sizeof(format), "text");
    if (strcmp(format, "text") != 0 && strcmp(format, "html") != 0) {
        snprintf(output, output_size, "错误：format 仅支持 text 或 html");
        cJSON_Delete(input);
        return ERR_INVALID_ARG;
    }

    cJSON_Delete(input);

    host_http_response_t resp = {0};
    struct curl_slist *req_headers = NULL;
    req_headers = curl_slist_append(req_headers,
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36");
    req_headers = curl_slist_append(req_headers, "Accept: text/html,application/xhtml+xml,text/plain;q=0.8,*/*;q=0.1");
    req_headers = curl_slist_append(req_headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8");
    err_t err = host_http_request("GET", url, req_headers, NULL, WEBFETCH_TIMEOUT_MS, &resp);
    curl_slist_free_all(req_headers);

    if (err != 0) {
        webfetch_report_error(url, resp.error, output, output_size);
        host_http_response_free(&resp);
        return err;
    }
    if (resp.status != 200) {
        webfetch_report_http_error(url, resp.status, output, output_size);
        host_http_response_free(&resp);
        return ERR_FAIL;
    }
    if (!resp.body || resp.body_len == 0) {
        snprintf(output, output_size, "错误：响应内容为空");
        host_http_response_free(&resp);
        return ERR_FAIL;
    }

    err_t result = webfetch_format_output(resp.body, resp.body_len, format, output, output_size);
    host_http_response_free(&resp);
    return result;
}

const struct tool *tool_webfetch_definition(void)
{
    return &s_webfetch_tool;
}
