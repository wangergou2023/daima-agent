/* 文件型会话存储后端。 */

#include "session_store.h"
#include "paths.h"
#include "runtime.h"
#include "autoconf.h"
#include "drivers/memory/session_store_file_internal.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
static bool is_compaction_summary_content(const cJSON *content)
{
    const char *text = cJSON_IsString((cJSON *)content) ? content->valuestring : NULL;
    if (!text) {
        return false;
    }
    return strncmp(text, "[上下文压缩摘要]", strlen("[上下文压缩摘要]")) == 0;
}

daima_err_t session_store_file_artifact_path(const char *chat_id,
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

static daima_err_t file_init(void)
{
    pr_info("Session manager initialized at %s", daima_path_session_dir());
    return DAIMA_OK;
}

static daima_err_t file_append_ex(const char *chat_id,
                                 const char *role,
                                 const char *content,
                                 const char *source)
{
    char path[BUF_SMALL];
    daima_err_t path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        pr_err("Cannot open session file %s", path);
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
        kfree(line);
    }

    fclose(f);
    return DAIMA_OK;
}

static daima_err_t file_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[BUF_SMALL];
    daima_err_t path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
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
    if (effective_max > SESSION_MAX_MSGS) {
        effective_max = SESSION_MAX_MSGS;
    }
    if (effective_max < 1) {
        effective_max = 1;
    }

    cJSON *messages[SESSION_MAX_MSGS];
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
        kfree(json_str);
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

    char path[BUF_SMALL];
    daima_err_t path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        pr_err("Cannot rewrite session file %s", path);
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
        kfree(line);
    }

    fclose(f);
    pr_info("Session %s rewritten", chat_id);
    return DAIMA_OK;
}

static daima_err_t file_clear(const char *chat_id)
{
    char path[BUF_SMALL];
    char facts[BUF_SMALL];
    char summary[BUF_SMALL];

    daima_err_t path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != DAIMA_OK) {
        return path_err;
    }
    path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_FACTS, facts, sizeof(facts));
    if (path_err != DAIMA_OK) {
        return path_err;
    }
    path_err = session_store_file_artifact_path(chat_id, DAIMA_SESSION_ARTIFACT_SUMMARY, summary, sizeof(summary));
    if (path_err != DAIMA_OK) {
        return path_err;
    }

    if (remove(path) == 0) {
        remove(facts);
        remove(summary);
        pr_info("Session %s cleared", chat_id);
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

struct session_record_node {
    struct list_head list;
    daima_session_record_t *record;
};

static daima_session_record_t *find_or_add_record(struct list_head *record_list,
                                                 struct session_record_node *nodes,
                                                 daima_session_record_t *records,
                                                 int *count,
                                                 size_t capacity,
                                                 const char *chat_id)
{
    struct session_record_node *node;
    list_for_each_entry(node, record_list, list, struct session_record_node) {
        if (strcmp(node->record->chat_id, chat_id) == 0) {
            return node->record;
        }
    }
    if ((size_t)*count >= capacity) {
        return NULL;
    }
    daima_session_record_t *record = &records[*count];
    memset(record, 0, sizeof(*record));
    strscpy(record->chat_id, chat_id, sizeof(record->chat_id));
    nodes[*count].record = record;
    INIT_LIST_HEAD(&nodes[*count].list);
    list_add(&nodes[*count].list, record_list);
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
    LIST_HEAD(record_list);
    struct session_record_node nodes[capacity];
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

        daima_session_record_t *record = find_or_add_record(&record_list, nodes, records, &count, capacity, chat_id);
        if (!record) {
            continue;
        }

        char path[BUF_SMALL];
        if (session_store_file_artifact_path(chat_id, kind, path, sizeof(path)) != DAIMA_OK) {
            continue;
        }

        if (kind == DAIMA_SESSION_ARTIFACT_HISTORY) {
            record->has_history = true;
            strscpy(record->history_path, path, sizeof(record->history_path));
        } else if (kind == DAIMA_SESSION_ARTIFACT_FACTS) {
            record->has_facts = true;
            strscpy(record->facts_path, path, sizeof(record->facts_path));
        } else {
            record->has_summary = true;
            strscpy(record->summary_path, path, sizeof(record->summary_path));
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
    .read_facts = session_store_file_read_facts,
    .merge_facts = session_store_file_merge_facts,
    .read_summary = session_store_file_read_summary,
    .write_summary = session_store_file_write_summary,
    .clear = file_clear,
    .list_records = file_list_records,
    .artifact_path = session_store_file_artifact_path,
};

const daima_session_store_ops_t *session_store_file_backend(void)
{
    return &s_file_backend;
}
