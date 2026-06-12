#include "memory/session_store_file_internal.h"

#include <stdio.h>
#include <time.h>

#include "daima_log.h"

static const char *TAG = "session_summary";

daima_err_t session_store_file_read_summary(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[DAIMA_BUF_SMALL];
    daima_err_t path_err = session_store_file_artifact_path(
        chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        buf[0] = '\0';
        return DAIMA_OK;
    }

    size_t n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return DAIMA_OK;
}

daima_err_t session_store_file_write_summary(const char *chat_id, const char *summary_text)
{
    if (!chat_id || !summary_text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[DAIMA_BUF_SMALL];
    daima_err_t path_err = session_store_file_artifact_path(
        chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        DAIMA_LOGE(TAG, "Cannot write session summary %s", path);
        return DAIMA_FAIL;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %Z", &tm_info);

    fprintf(f, "## 最近一次上下文压缩摘要\n");
    fprintf(f, "更新时间：%s\n\n", time_buf);
    fprintf(f, "%s\n", summary_text);
    fclose(f);
    DAIMA_LOGI(TAG, "Session %s summary updated", chat_id);
    return DAIMA_OK;
}
