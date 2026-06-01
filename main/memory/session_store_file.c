/* 文件型会话存储后端。 */

#include "session_store.h"
#include "app/daima_paths.h"
#include "app/runtime_config.h"
#include "daima_config.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "session";

#define SESSION_FACTS_MAX_BYTES   4096
#define SESSION_FACTS_MAX_LINES   12
#define SESSION_FACTS_LINE_BYTES  256

static bool is_compaction_summary_content(const cJSON *content)
{
    const char *text = cJSON_IsString((cJSON *)content) ? content->valuestring : NULL;
    if (!text) {
        return false;
    }
    return strncmp(text, "[上下文压缩摘要]", strlen("[上下文压缩摘要]")) == 0;
}

static daima_err_t file_artifact_path(const char *chat_id,
                                     daima_session_artifact_kind_t kind,
                                     char *buf,
                                     size_t size)
{
    if (!chat_id || !chat_id[0] || !buf || size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *suffix = ".jsonl";
    if (kind == DAIMA_SESSION_ARTIFACT_FACTS) {
        suffix = "_facts.md";
    } else if (kind == DAIMA_SESSION_ARTIFACT_SUMMARY) {
        suffix = "_summary.md";
    }

    snprintf(buf, size, "%s/session_%s%s", daima_path_session_dir(), chat_id, suffix);
    return DAIMA_OK;
}

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
            snprintf(lines[count], SESSION_FACTS_LINE_BYTES, "%s", norm);
            count++;
        } else {
            for (int i = 1; i < SESSION_FACTS_MAX_LINES; i++) {
                snprintf(lines[i - 1], SESSION_FACTS_LINE_BYTES, "%s", lines[i]);
            }
            snprintf(lines[SESSION_FACTS_MAX_LINES - 1], SESSION_FACTS_LINE_BYTES, "%s", norm);
        }
    }

    free(copy);
    return count;
}

static daima_err_t file_init(void)
{
    DAIMA_LOGI(TAG, "Session manager initialized at %s", daima_path_session_dir());
    return DAIMA_OK;
}

static daima_err_t file_append_ex(const char *chat_id,
                                 const char *role,
                                 const char *content,
                                 const char *source)
{
    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        DAIMA_LOGE(TAG, "Cannot open session file %s", path);
        return DAIMA_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    if (source && source[0]) {
        cJSON_AddStringToObject(obj, "source", source);
    }
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);
    return DAIMA_OK;
}

static daima_err_t file_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(buf, size, "[]");
        return DAIMA_OK;
    }

    int configured_max = runtime_config_get_session_max_msgs();
    int effective_max = max_msgs;
    if (effective_max <= 0 || effective_max > configured_max) {
        effective_max = configured_max;
    }
    if (effective_max > DAIMA_SESSION_MAX_MSGS) {
        effective_max = DAIMA_SESSION_MAX_MSGS;
    }
    if (effective_max < 1) {
        effective_max = 1;
    }

    cJSON *messages[DAIMA_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    char line[16384];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        if (is_compaction_summary_content(cJSON_GetObjectItem(obj, "content"))) {
            cJSON_Delete(obj);
            continue;
        }

        if (count >= effective_max) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % effective_max;
        if (count < effective_max) count++;
    }
    fclose(f);

    cJSON *arr = cJSON_CreateArray();
    int start = (count < effective_max) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % effective_max;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        cJSON *source = cJSON_GetObjectItem(src, "source");
        if (role && content) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            if (source && cJSON_IsString(source) && source->valuestring && source->valuestring[0]) {
                cJSON_AddStringToObject(entry, "source", source->valuestring);
            }
        }
        cJSON_AddItemToArray(arr, entry);
    }

    int cleanup_start = (count < effective_max) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % effective_max;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return DAIMA_OK;
}

static daima_err_t file_rewrite_from_array(const char *chat_id, const cJSON *messages)
{
    if (!chat_id || !messages || !cJSON_IsArray(messages)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        DAIMA_LOGE(TAG, "Cannot rewrite session file %s", path);
        return DAIMA_FAIL;
    }

    const cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem((cJSON *)msg, "role");
        cJSON *content = cJSON_GetObjectItem((cJSON *)msg, "content");
        if (!role || !cJSON_IsString(role) || !content || !cJSON_IsString(content)) {
            continue;
        }
        if (is_compaction_summary_content(content)) {
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            fclose(f);
            return DAIMA_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(obj, "role", role->valuestring);
        cJSON_AddStringToObject(obj, "content", content->valuestring);
        cJSON *source = cJSON_GetObjectItem((cJSON *)msg, "source");
        if (source && cJSON_IsString(source) && source->valuestring && source->valuestring[0]) {
            cJSON_AddStringToObject(obj, "source", source->valuestring);
        }
        cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

        char *line = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!line) {
            fclose(f);
            return DAIMA_ERR_NO_MEM;
        }

        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);
    DAIMA_LOGI(TAG, "Session %s rewritten", chat_id);
    return DAIMA_OK;
}

static daima_err_t file_read_facts(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_FACTS, path, sizeof(path));
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

static daima_err_t file_merge_facts(const char *chat_id, const char *facts_text)
{
    if (!chat_id || !facts_text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char existing[SESSION_FACTS_MAX_BYTES];
    existing[0] = '\0';
    file_read_facts(chat_id, existing, sizeof(existing));

    char lines[SESSION_FACTS_MAX_LINES][SESSION_FACTS_LINE_BYTES];
    memset(lines, 0, sizeof(lines));

    int count = 0;
    count = append_unique_fact_lines(lines, count, existing);
    count = append_unique_fact_lines(lines, count, facts_text);
    if (count <= 0) {
        return DAIMA_OK;
    }

    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_FACTS, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        DAIMA_LOGE(TAG, "Cannot write session facts %s", path);
        return DAIMA_FAIL;
    }

    fprintf(f, "## 会话事实卡片\n");
    for (int i = 0; i < count; i++) {
        fprintf(f, "- %s\n", lines[i]);
    }
    fclose(f);
    DAIMA_LOGI(TAG, "Session %s facts merged: %d lines", chat_id, count);
    return DAIMA_OK;
}

static daima_err_t file_read_summary(const char *chat_id, char *buf, size_t size)
{
    if (!chat_id || !buf || size == 0) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
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

static daima_err_t file_write_summary(const char *chat_id, const char *summary_text)
{
    if (!chat_id || !summary_text) {
        return DAIMA_ERR_INVALID_ARG;
    }

    char path[256];
    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, path, sizeof(path));
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

static daima_err_t file_clear(const char *chat_id)
{
    char path[256];
    char facts[256];
    char summary[256];

    daima_err_t path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }
    path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_FACTS, facts, sizeof(facts));
    if (path_err != DAIMA_OK) {
        return path_err;
    }
    path_err = file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, summary, sizeof(summary));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    if (remove(path) == 0) {
        remove(facts);
        remove(summary);
        DAIMA_LOGI(TAG, "Session %s cleared", chat_id);
        return DAIMA_OK;
    }
    return DAIMA_ERR_NOT_FOUND;
}

static bool parse_session_filename_with_suffix(const char *filename,
                                               const char *suffix,
                                               char *chat_id,
                                               size_t chat_id_size)
{
    const char *prefix = "session_";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t len = filename ? strlen(filename) : 0;

    if (!filename || len <= prefix_len + suffix_len) {
        return false;
    }
    if (strncmp(filename, prefix, prefix_len) != 0) {
        return false;
    }
    if (strcmp(filename + len - suffix_len, suffix) != 0) {
        return false;
    }

    size_t chat_len = len - prefix_len - suffix_len;
    if (chat_len == 0 || chat_len >= chat_id_size) {
        return false;
    }
    memcpy(chat_id, filename + prefix_len, chat_len);
    chat_id[chat_len] = '\0';
    return true;
}

static daima_session_record_t *find_or_add_record(daima_session_record_t *records,
                                                 int *count,
                                                 size_t capacity,
                                                 const char *chat_id)
{
    for (int i = 0; i < *count; i++) {
        if (strcmp(records[i].chat_id, chat_id) == 0) {
            return &records[i];
        }
    }
    if ((size_t)*count >= capacity) {
        return NULL;
    }
    daima_session_record_t *record = &records[*count];
    memset(record, 0, sizeof(*record));
    snprintf(record->chat_id, sizeof(record->chat_id), "%s", chat_id);
    (*count)++;
    return record;
}

static void maybe_update_record_mtime(daima_session_record_t *record, const char *path)
{
    if (!record || !path || !path[0]) {
        return;
    }
    struct stat st;
    if (stat(path, &st) == 0 && st.st_mtime > record->latest_ts) {
        record->latest_ts = st.st_mtime;
    }
}

static daima_err_t file_list_records(daima_session_record_t *records, size_t capacity, int *out_count)
{
    if (!records || capacity == 0 || !out_count) {
        return DAIMA_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(daima_path_session_dir());
    if (!dir) {
        return DAIMA_FAIL;
    }

    int count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        char chat_id[sizeof(records[0].chat_id)];
        daima_session_artifact_kind_t kind;

        if (parse_session_filename_with_suffix(entry->d_name, ".jsonl", chat_id, sizeof(chat_id))) {
            kind = DAIMA_SESSION_ARTIFACT_HISTORY;
        } else if (parse_session_filename_with_suffix(entry->d_name, "_facts.md", chat_id, sizeof(chat_id))) {
            kind = DAIMA_SESSION_ARTIFACT_FACTS;
        } else if (parse_session_filename_with_suffix(entry->d_name, "_summary.md", chat_id, sizeof(chat_id))) {
            kind = DAIMA_SESSION_ARTIFACT_SUMMARY;
        } else {
            continue;
        }

        daima_session_record_t *record = find_or_add_record(records, &count, capacity, chat_id);
        if (!record) {
            continue;
        }

        char path[256];
        if (file_artifact_path(chat_id, kind, path, sizeof(path)) != DAIMA_OK) {
            continue;
        }

        if (kind == DAIMA_SESSION_ARTIFACT_HISTORY) {
            record->has_history = true;
            snprintf(record->history_path, sizeof(record->history_path), "%s", path);
        } else if (kind == DAIMA_SESSION_ARTIFACT_FACTS) {
            record->has_facts = true;
            snprintf(record->facts_path, sizeof(record->facts_path), "%s", path);
        } else {
            record->has_summary = true;
            snprintf(record->summary_path, sizeof(record->summary_path), "%s", path);
        }
        maybe_update_record_mtime(record, path);
    }

    closedir(dir);
    *out_count = count;
    return DAIMA_OK;
}

static const daima_session_store_ops_t s_file_backend = {
    .init = file_init,
    .append_ex = file_append_ex,
    .get_history_json = file_get_history_json,
    .rewrite_from_array = file_rewrite_from_array,
    .read_facts = file_read_facts,
    .merge_facts = file_merge_facts,
    .read_summary = file_read_summary,
    .write_summary = file_write_summary,
    .clear = file_clear,
    .list_records = file_list_records,
    .artifact_path = file_artifact_path,
};

const daima_session_store_ops_t *session_store_file_backend(void)
{
    return &s_file_backend;
}
