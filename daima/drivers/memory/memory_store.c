/* 记忆存储（长期记忆读写）。 */

#include "memory_store.h"
#include "paths.h"
#include "autoconf.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "linux/printk.h"
static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

daima_err_t memory_store_init(void)
{
    /* SPIFFS 是扁平结构——不需要真正创建目录。
       这里只需确认能打开 base 路径。 */
    pr_info("Memory store initialized at %s", daima_path_spiffs_base());
    return DAIMA_OK;
}

daima_err_t memory_read_long_term(char *buf, size_t size)
{
    FILE *f = fopen(daima_path_memory_file(), "r");
    if (!f) {
        buf[0] = '\0';
        return DAIMA_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return DAIMA_OK;
}

daima_err_t memory_write_long_term(const char *content)
{
    FILE *f = fopen(daima_path_memory_file(), "w");
    if (!f) {
        pr_err("Cannot write %s", daima_path_memory_file());
        return DAIMA_FAIL;
    }
    fputs(content, f);
    fclose(f);
    pr_info("Long-term memory updated (%d bytes)", (int)strlen(content));
    return DAIMA_OK;
}

daima_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char path[BUF_SMALL];
    snprintf(path, sizeof(path), "%s/%s.md", daima_path_memory_dir(), date_str);

    FILE *f = fopen(path, "a");
    if (!f) {
        /* 尝试创建——如果文件不存在则写入标题 */
        f = fopen(path, "w");
        if (!f) {
            pr_err("Cannot open %s", path);
            return DAIMA_FAIL;
        }
        fprintf(f, "# %s\n\n", date_str);
    }

    fprintf(f, "%s\n", note);
    fclose(f);
    return DAIMA_OK;
}

daima_err_t memory_read_recent(char *buf, size_t size, int days)
{
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char path[BUF_SMALL];
        snprintf(path, sizeof(path), "%s/%s.md", daima_path_memory_dir(), date_str);

        FILE *f = fopen(path, "r");
        if (!f) {
            snprintf(path, sizeof(path), "%s/daily/%s.md", daima_path_memory_dir(), date_str);
            f = fopen(path, "r");
        }
        if (!f) continue;

        if (offset > 0 && offset < size - 4) {
            offset += snprintf(buf + offset, size - offset, "\n---\n");
        }

        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }

    return DAIMA_OK;
}
