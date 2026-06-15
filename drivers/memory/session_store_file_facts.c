#include "drivers/memory/session_store_file_internal.h"
#include "drivers/memory/session_store_file_common.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#define SESSION_FACTS_MAX_BYTES   4096
#define SESSION_FACTS_MAX_LINES   12
#define SESSION_FACTS_LINE_BYTES  256

static char *trim_ascii(char *s)
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }

    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        s[--len] = '\0';
    }
    return s;
}

static char *normalize_fact_line(char *line)
{
    char *s = trim_ascii(line);
    if ((s[0] == '-' || s[0] == '*' || s[0] == '+') &&
        (s[1] == ' ' || s[1] == '\t')) {
        s = trim_ascii(s + 1);
    }
    return s;
}

static int append_unique_fact_lines(
    char lines[][SESSION_FACTS_LINE_BYTES],
    int count,
    const char *text)
{
    if (!text || !text[0]) {
        return count;
    }

    char *copy = strdup(text);
    if (!copy) {
        return count;
    }

    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr);
         line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *norm = normalize_fact_line(line);
        if (!norm[0] || strncmp(norm, "##", 2) == 0) {
            continue;
        }

        bool exists = false;
        for (int i = 0; i < count; i++) {
            if (strcmp(lines[i], norm) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        if (count < SESSION_FACTS_MAX_LINES) {
            strscpy(lines[count], norm, SESSION_FACTS_LINE_BYTES);
            count++;
        } else {
            for (int i = 1; i < SESSION_FACTS_MAX_LINES; i++) {
                strscpy(lines[i - 1], lines[i], SESSION_FACTS_LINE_BYTES);
            }
            strscpy(lines[SESSION_FACTS_MAX_LINES - 1], norm, SESSION_FACTS_LINE_BYTES);
        }
    }

    kfree(copy);
    return count;
}

err_t session_store_file_read_facts(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(
        chat_id, SESSION_ARTIFACT_FACTS, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    if (!session_file_read_all(path, buf, size, NULL)) {
        buf[0] = '\0';
    }
    return 0;
}

err_t session_store_file_merge_facts(const char *chat_id, const char *facts_text)
{
    if (!chat_id || !facts_text) {
        return ERR_INVALID_ARG;
    }

    char existing[SESSION_FACTS_MAX_BYTES];
    existing[0] = '\0';
    session_store_file_read_facts(chat_id, existing, sizeof(existing));

    char lines[SESSION_FACTS_MAX_LINES][SESSION_FACTS_LINE_BYTES];
    memset(lines, 0, sizeof(lines));

    int count = 0;
    count = append_unique_fact_lines(lines, count, existing);
    count = append_unique_fact_lines(lines, count, facts_text);
    if (count <= 0) {
        return 0;
    }

    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(
        chat_id, SESSION_ARTIFACT_FACTS, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        pr_err("Cannot write session facts %s", path);
        return ERR_FAIL;
    }

    fprintf(f, "## 会话事实卡片\n");
    for (int i = 0; i < count; i++) {
        fprintf(f, "- %s\n", lines[i]);
    }
    fclose(f);
    pr_info("Session %s facts merged: %d lines", chat_id, count);
    return 0;
}
