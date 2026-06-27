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
#include <sys/file.h>

#include "cjson.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"

static void session_history_make_id(char *buf,
                                    size_t size,
                                    const char *role,
                                    const char *source,
                                    const char *content,
                                    double ts,
                                    int ordinal)
{
    unsigned long hash = 2166136261u;
    const unsigned char *ptr = NULL;
    const char *parts[] = {
        role ? role : "",
        source ? source : "",
        content ? content : "",
    };

    if (!buf || size == 0) {
        return;
    }

    for (size_t part_idx = 0; part_idx < sizeof(parts) / sizeof(parts[0]); part_idx++) {
        for (ptr = (const unsigned char *)parts[part_idx]; ptr && *ptr; ptr++) {
            hash ^= (unsigned long)(*ptr);
            hash *= 16777619u;
        }
        hash ^= (unsigned long)'|';
        hash *= 16777619u;
    }

    snprintf(buf, size, "hist-%lld-%d-%08lx",
             (long long)ts,
             ordinal,
             hash);
}

static bool is_compaction_summary_content(const cJSON *content)
{
    const char *text = cJSON_IsString((cJSON *)content) ? content->valuestring : NULL;
    if (!text) {
        return false;
    }
    return strncmp(text, "[上下文压缩摘要]", strlen("[上下文压缩摘要]")) == 0;
}

static unsigned long session_history_resolve_seq(const cJSON *obj, int ordinal)
{
    cJSON *seq = NULL;

    if (!obj) {
        return ordinal > 0 ? (unsigned long)ordinal : 0;
    }

    seq = cJSON_GetObjectItem((cJSON *)obj, "seq");
    if (seq && cJSON_IsNumber(seq) && seq->valuedouble > 0) {
        return (unsigned long)seq->valuedouble;
    }

    return ordinal > 0 ? (unsigned long)ordinal : 0;
}

static unsigned long session_history_next_seq(FILE *f)
{
    long original_pos = 0;
    unsigned long max_seq = 0;
    int ordinal = 0;
    char line[16384];

    if (!f) {
        return 1;
    }

    original_pos = ftell(f);
    if (original_pos < 0) {
        original_pos = 0;
    }
    rewind(f);

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        cJSON *obj = NULL;
        unsigned long seq_value = 0;

        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        obj = cJSON_Parse(line);
        if (!obj) continue;
        if (is_compaction_summary_content(cJSON_GetObjectItem(obj, "content"))) {
            cJSON_Delete(obj);
            continue;
        }

        ordinal++;
        seq_value = session_history_resolve_seq(obj, ordinal);
        if (seq_value > max_seq) {
            max_seq = seq_value;
        }
        cJSON_Delete(obj);
    }

    fseek(f, original_pos, SEEK_SET);
    return max_seq > 0 ? max_seq + 1 : 1;
}

static const char *session_history_resolve_id(const cJSON *obj,
                                              char *buf,
                                              size_t size,
                                              int ordinal)
{
    cJSON *id = NULL;
    cJSON *role = NULL;
    cJSON *content = NULL;
    cJSON *source = NULL;
    cJSON *ts = NULL;

    if (!obj) {
        return "";
    }

    id = cJSON_GetObjectItem((cJSON *)obj, "id");
    if (id && cJSON_IsString(id) && id->valuestring && id->valuestring[0]) {
        return id->valuestring;
    }

    role = cJSON_GetObjectItem((cJSON *)obj, "role");
    content = cJSON_GetObjectItem((cJSON *)obj, "content");
    source = cJSON_GetObjectItem((cJSON *)obj, "source");
    ts = cJSON_GetObjectItem((cJSON *)obj, "ts");
    session_history_make_id(buf,
                            size,
                            cJSON_IsString(role) ? role->valuestring : "",
                            cJSON_IsString(source) ? source->valuestring : "",
                            cJSON_IsString(content) ? content->valuestring : "",
                            cJSON_IsNumber(ts) ? ts->valuedouble : 0,
                            ordinal);
    return buf;
}

err_t session_store_file_artifact_path(const char *chat_id,
                                             session_artifact_kind_t kind,
                                             char *buf,
                                             size_t size)
{
    if (!chat_id || !chat_id[0] || !buf || size == 0) {
        return ERR_INVALID_ARG;
    }

    const char *suffix = ".jsonl";
    if (kind == SESSION_ARTIFACT_FACTS) {
        suffix = "_facts.md";
    } else if (kind == SESSION_ARTIFACT_SUMMARY) {
        suffix = "_summary.md";
    }

    snprintf(buf, size, "%s/session_%s%s", path_session_dir(), chat_id, suffix);
    return 0;
}

static err_t file_init(void)
{
    pr_info("Session manager initialized at %s", path_session_dir());
    return 0;
}

static err_t file_append_ex(const char *chat_id,
                                 const char *role,
                                 const char *content,
                                 const char *source)
{
    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    FILE *f = fopen(path, "a+");
    if (!f) {
        pr_err("Cannot open session file %s", path);
        return ERR_FAIL;
    }
    flock(fileno(f), LOCK_EX);

    cJSON *obj = cJSON_CreateObject();
    char entry_id[64];
    double now_ts = (double)time(NULL);
    unsigned long next_seq = 1;

    next_seq = session_history_next_seq(f);

    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    if (source && source[0]) {
        cJSON_AddStringToObject(obj, "source", source);
    }
    session_history_make_id(entry_id, sizeof(entry_id), role, source, content, now_ts, 0);
    cJSON_AddStringToObject(obj, "id", entry_id);
    cJSON_AddNumberToObject(obj, "seq", (double)next_seq);
    cJSON_AddNumberToObject(obj, "ts", now_ts);

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        kfree(line);
    }

    fclose(f);
    return 0;
}

static err_t file_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(buf, size, "[]");
        return 0;
    }
    flock(fileno(f), LOCK_SH);

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
    int line_no = 0;

    char line[16384];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;
        line_no++;

        if (is_compaction_summary_content(cJSON_GetObjectItem(obj, "content"))) {
            cJSON_Delete(obj);
            continue;
        }

        if (!cJSON_GetObjectItem(obj, "id")) {
            char entry_id[64];
            const char *resolved_id = session_history_resolve_id(obj, entry_id, sizeof(entry_id), line_no);
            if (resolved_id && resolved_id[0]) {
                cJSON_AddStringToObject(obj, "id", resolved_id);
            }
        }
        if (!cJSON_GetObjectItem(obj, "seq")) {
            cJSON_AddNumberToObject(obj, "seq", (double)session_history_resolve_seq(obj, line_no));
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
        cJSON *id = cJSON_GetObjectItem(src, "id");
        cJSON *source = cJSON_GetObjectItem(src, "source");
        cJSON *ts = cJSON_GetObjectItem(src, "ts");
        char entry_id[64];
        const char *resolved_id = NULL;
        if (role && content) {
            if (id && cJSON_IsString(id) && id->valuestring && id->valuestring[0]) {
                resolved_id = id->valuestring;
            } else {
                resolved_id = session_history_resolve_id(src, entry_id, sizeof(entry_id), start + i + 1);
            }
            if (resolved_id && resolved_id[0]) {
                cJSON_AddStringToObject(entry, "id", resolved_id);
            }
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            cJSON_AddNumberToObject(entry, "seq",
                                    (double)session_history_resolve_seq(src, start + i + 1));
            if (source && cJSON_IsString(source) && source->valuestring && source->valuestring[0]) {
                cJSON_AddStringToObject(entry, "source", source->valuestring);
            }
            if (ts && cJSON_IsNumber(ts)) {
                cJSON_AddNumberToObject(entry, "ts", ts->valuedouble);
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

    return 0;
}

static err_t file_rewrite_from_array(const char *chat_id, const cJSON *messages)
{
    if (!chat_id || !messages || !cJSON_IsArray(messages)) {
        return ERR_INVALID_ARG;
    }

    char path[BUF_SMALL];
    err_t path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        pr_err("Cannot rewrite session file %s", path);
        return ERR_FAIL;
    }
    flock(fileno(f), LOCK_EX);

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
        char entry_id[64];
        const char *resolved_id = NULL;
        cJSON *source = NULL;
        cJSON *id = NULL;
        cJSON *ts = NULL;
        double ts_value = 0;
        unsigned long seq_value = 0;
        if (!obj) {
            fclose(f);
            return ERR_NO_MEM;
        }
        source = cJSON_GetObjectItem((cJSON *)msg, "source");
        id = cJSON_GetObjectItem((cJSON *)msg, "id");
        ts = cJSON_GetObjectItem((cJSON *)msg, "ts");
        ts_value = (ts && cJSON_IsNumber(ts)) ? ts->valuedouble : (double)time(NULL);
        seq_value = session_history_resolve_seq((cJSON *)msg, 0);
        cJSON_AddStringToObject(obj, "role", role->valuestring);
        cJSON_AddStringToObject(obj, "content", content->valuestring);
        if (source && cJSON_IsString(source) && source->valuestring && source->valuestring[0]) {
            cJSON_AddStringToObject(obj, "source", source->valuestring);
        }
        if (id && cJSON_IsString(id) && id->valuestring && id->valuestring[0]) {
            resolved_id = id->valuestring;
        } else {
            resolved_id = session_history_resolve_id((cJSON *)msg, entry_id, sizeof(entry_id), 0);
        }
        if (resolved_id && resolved_id[0]) {
            cJSON_AddStringToObject(obj, "id", resolved_id);
        }
        if (seq_value > 0) {
            cJSON_AddNumberToObject(obj, "seq", (double)seq_value);
        }
        cJSON_AddNumberToObject(obj, "ts", ts_value);

        char *line = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!line) {
            fclose(f);
            return ERR_NO_MEM;
        }

        fprintf(f, "%s\n", line);
        kfree(line);
    }

    fclose(f);
    pr_info("Session %s rewritten", chat_id);
    return 0;
}

static err_t file_clear(const char *chat_id)
{
    char path[BUF_SMALL];
    char facts[BUF_SMALL];
    char summary[BUF_SMALL];

    err_t path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_HISTORY, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_FACTS, facts, sizeof(facts));
    if (path_err != 0) {
        return path_err;
    }
    path_err = session_store_file_artifact_path(chat_id, SESSION_ARTIFACT_SUMMARY, summary, sizeof(summary));
    if (path_err != 0) {
        return path_err;
    }

    if (remove(path) == 0) {
        remove(facts);
        remove(summary);
        pr_info("Session %s cleared", chat_id);
        return 0;
    }
    return ERR_NOT_FOUND;
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
    session_record_t *record;
};

static session_record_t *find_or_add_record(struct list_head *record_list,
                                                 struct session_record_node *nodes,
                                                 session_record_t *records,
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
    session_record_t *record = &records[*count];
    memset(record, 0, sizeof(*record));
    strscpy(record->chat_id, chat_id, sizeof(record->chat_id));
    nodes[*count].record = record;
    INIT_LIST_HEAD(&nodes[*count].list);
    list_add(&nodes[*count].list, record_list);
    (*count)++;
    return record;
}

static void maybe_update_record_mtime(session_record_t *record, const char *path)
{
    if (!record || !path || !path[0]) {
        return;
    }
    struct stat st;
    if (stat(path, &st) == 0 && st.st_mtime > record->latest_ts) {
        record->latest_ts = st.st_mtime;
    }
}

static err_t file_list_records(session_record_t *records, size_t capacity, int *out_count)
{
    if (!records || capacity == 0 || !out_count) {
        return ERR_INVALID_ARG;
    }

    DIR *dir = opendir(path_session_dir());
    if (!dir) {
        return ERR_FAIL;
    }

    int count = 0;
    LIST_HEAD(record_list);
    struct session_record_node nodes[capacity];
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        char chat_id[sizeof(records[0].chat_id)];
        session_artifact_kind_t kind;

        if (parse_session_filename_with_suffix(entry->d_name, ".jsonl", chat_id, sizeof(chat_id))) {
            kind = SESSION_ARTIFACT_HISTORY;
        } else if (parse_session_filename_with_suffix(entry->d_name, "_facts.md", chat_id, sizeof(chat_id))) {
            kind = SESSION_ARTIFACT_FACTS;
        } else if (parse_session_filename_with_suffix(entry->d_name, "_summary.md", chat_id, sizeof(chat_id))) {
            kind = SESSION_ARTIFACT_SUMMARY;
        } else {
            continue;
        }

        session_record_t *record = find_or_add_record(&record_list, nodes, records, &count, capacity, chat_id);
        if (!record) {
            continue;
        }

        char path[BUF_SMALL];
        if (session_store_file_artifact_path(chat_id, kind, path, sizeof(path)) != 0) {
            continue;
        }

        if (kind == SESSION_ARTIFACT_HISTORY) {
            record->has_history = true;
            strscpy(record->history_path, path, sizeof(record->history_path));
        } else if (kind == SESSION_ARTIFACT_FACTS) {
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
    return 0;
}

static const session_store_ops_t s_file_backend = {
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

const session_store_ops_t *session_store_file_backend(void)
{
    return &s_file_backend;
}
