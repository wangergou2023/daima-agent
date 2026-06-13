/* 飞书消息图片下载。 */

#include "drivers/channel/feishu/feishu_media.h"
#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include "http.h"
#include "paths.h"
#include "autoconf.h"
#include "linux/printk.h"
#define FEISHU_API_BASE         "https://open.feishu.cn/open-apis"
#define FEISHU_MSG_RESOURCE_URL FEISHU_API_BASE "/im/v1/messages/%s/resources/%s?type=%s"

static const char *header_value_ci(const char *headers, const char *name)
{
    static char value[128];
    value[0] = '\0';
    if (!headers || !name) {
        return NULL;
    }

    size_t name_len = strlen(name);
    const char *p = headers;
    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > name_len + 1 &&
            strncasecmp(p, name, name_len) == 0 &&
            p[name_len] == ':') {
            const char *v = p + name_len + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t copy = line_len - (size_t)(v - p);
            if (copy >= sizeof(value)) copy = sizeof(value) - 1;
            memcpy(value, v, copy);
            value[copy] = '\0';
            return value;
        }
        if (!line_end) break;
        p = line_end + 2;
    }
    return NULL;
}

static const char *guess_image_ext_from_type(const char *content_type)
{
    if (!content_type) return ".jpg";
    if (strstr(content_type, "png")) return ".png";
    if (strstr(content_type, "webp")) return ".webp";
    if (strstr(content_type, "gif")) return ".gif";
    if (strstr(content_type, "bmp")) return ".bmp";
    return ".jpg";
}

char *feishu_download_message_image(const char *tenant_token,
                                    const char *message_id,
                                    const char *image_key)
{
    if (!tenant_token || !tenant_token[0] ||
        !message_id || !message_id[0] ||
        !image_key || !image_key[0]) {
        return NULL;
    }

    char url[1024];
    snprintf(url, sizeof(url), FEISHU_MSG_RESOURCE_URL, message_id, image_key, "image");

    struct curl_slist *headers = NULL;
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", tenant_token);
    headers = curl_slist_append(headers, auth_header);

    host_http_response_t resp = {0};
    daima_err_t err = host_http_request("GET", url, headers, NULL, 15000, &resp);
    curl_slist_free_all(headers);
    if (err != DAIMA_OK || resp.status != 200 || !resp.body || resp.body_len == 0) {
        pr_warn("Failed to download Feishu image: msg=%s key=%s http=%ld err=%s", message_id, image_key, resp.status, daima_err_to_name(err));
        host_http_response_free(&resp);
        return NULL;
    }

    daima_fs_ensure_dir(daima_path_cache_dir());
    daima_fs_ensure_dir(daima_path_feishu_image_dir());

    const char *content_type = header_value_ci(resp.headers, "Content-Type");
    const char *ext = guess_image_ext_from_type(content_type);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%ld%s",
             daima_path_feishu_image_dir(), image_key, (long)time(NULL), ext);

    FILE *f = fopen(path, "wb");
    if (!f) {
        pr_warn("Cannot open image cache file: %s", path);
        host_http_response_free(&resp);
        return NULL;
    }

    size_t body_len = resp.body_len;
    size_t written = fwrite(resp.body, 1, body_len, f);
    fclose(f);
    host_http_response_free(&resp);
    if (written != body_len) {
        unlink(path);
        return NULL;
    }

    char *out = strdup(path);
    if (out) {
        pr_info("Cached Feishu image to %s", path);
    }
    return out;
}
