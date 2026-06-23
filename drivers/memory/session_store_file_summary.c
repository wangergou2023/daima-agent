#include "drivers/memory/session_store_file_internal.h"
#include "drivers/memory/session_store_file_common.h"

#include <stdio.h>
#include <time.h>

#include "linux/printk.h"
err_t session_store_file_read_summary(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(
        chat_id, SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    if (!session_file_read_all(path, buf, size, NULL)) {
        buf[0] = '\0';
    }
    return 0;
}

err_t session_store_file_write_summary(const char *chat_id, const char *summary_text)
{
    if (!chat_id || !summary_text) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(
        chat_id, SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %Z", &tm_info);

    char content[BUF_XLARGE];
    snprintf(content, sizeof(content),
             "## Latest Context Compression Summary\n"
             "Updated At: %s\n\n"
             "%s\n",
             time_buf, summary_text);

    if (!session_file_write_all(path, content)) {
        pr_err("Cannot write session summary %s", path);
        return ERR_FAIL;
    }
    pr_info("Session %s summary updated", chat_id);
    return 0;
}
