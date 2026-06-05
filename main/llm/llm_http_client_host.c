#include "llm/llm_http_client_host.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_http.h"
#include "daima_config.h"
#include "daima_log.h"

#define LLM_DUMP_MAX_BYTES   (16 * 1024)
#define LLM_DUMP_CHUNK_BYTES 320
#define LLM_HTTP_AUTH_HEADER_MAX 352

void llm_http_log_payload(const char *tag, const char *label, const char *payload)
{
    if (!payload) {
        DAIMA_LOGI(tag, "%s: <null>", label);
        return;
    }

    size_t total = strlen(payload);
#if DAIMA_LLM_LOG_VERBOSE_PAYLOAD
    size_t shown = total > LLM_DUMP_MAX_BYTES ? LLM_DUMP_MAX_BYTES : total;
    DAIMA_LOGI(tag, "%s (%u bytes)%s",
              label,
              (unsigned)total,
              (shown < total) ? " [truncated]" : "");

    char chunk[LLM_DUMP_CHUNK_BYTES + 1];
    for (size_t off = 0; off < shown; off += LLM_DUMP_CHUNK_BYTES) {
        size_t n = shown - off;
        if (n > LLM_DUMP_CHUNK_BYTES) {
            n = LLM_DUMP_CHUNK_BYTES;
        }
        memcpy(chunk, payload + off, n);
        chunk[n] = '\0';
        DAIMA_LOGI(tag, "%s[%u]: %s", label, (unsigned)off, chunk);
    }
#else
    if (DAIMA_LLM_LOG_PREVIEW_BYTES > 0) {
        size_t shown = total > DAIMA_LLM_LOG_PREVIEW_BYTES ? DAIMA_LLM_LOG_PREVIEW_BYTES : total;
        char preview[DAIMA_LLM_LOG_PREVIEW_BYTES + 1];
        memcpy(preview, payload, shown);
        preview[shown] = '\0';
        for (size_t i = 0; i < shown; i++) {
            if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
                preview[i] = ' ';
            }
        }
        DAIMA_LOGI(tag, "%s (%u bytes): %s%s",
                  label,
                  (unsigned)total,
                  preview,
                  (shown < total) ? " ..." : "");
    } else {
        DAIMA_LOGI(tag, "%s (%u bytes)", label, (unsigned)total);
    }
#endif
}

daima_err_t llm_http_post_json(const char *url,
                              const char *api_key,
                              const char *post_data,
                              int timeout_ms,
                              char **body_out,
                              int *status_out)
{
    if (!url || !post_data || !body_out || !status_out) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *body_out = NULL;
    *status_out = 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (api_key && api_key[0] && url && strstr(url, "/anthropic/")) {
        char key_header[LLM_HTTP_AUTH_HEADER_MAX];
        snprintf(key_header, sizeof(key_header), "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, key_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else if (api_key && api_key[0]) {
        char auth[LLM_HTTP_AUTH_HEADER_MAX];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth);
    }

    host_http_response_t resp = {0};
    daima_err_t err = host_http_request("POST", url, headers, post_data, timeout_ms, &resp);
    if (headers) {
        curl_slist_free_all(headers);
    }
    if (err != DAIMA_OK) {
        host_http_response_free(&resp);
        return err;
    }

    *status_out = (int)resp.status;
    if (resp.body) {
        *body_out = strdup(resp.body);
        if (!*body_out) {
            host_http_response_free(&resp);
            return DAIMA_ERR_NO_MEM;
        }
    } else {
        *body_out = strdup("");
        if (!*body_out) {
            host_http_response_free(&resp);
            return DAIMA_ERR_NO_MEM;
        }
    }

    host_http_response_free(&resp);
    return DAIMA_OK;
}
