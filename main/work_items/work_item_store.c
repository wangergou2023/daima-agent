#include "work_items/work_item_store.h"

#include "app/daima_fs.h"
#include "app/daima_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef WORK_ITEM_LINE_MAX
#define WORK_ITEM_LINE_MAX 16384
#endif

static bool str_in_set(const char *value, const char *const *set, size_t count)
{
    if (!value || !value[0]) return false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, set[i]) == 0) return true;
    }
    return false;
}

bool work_item_type_valid(const char *value)
{
    static const char *const values[] = {
        "defect", "missing", "improvement", "tech_debt", "docs", "test_gap",
    };
    return str_in_set(value, values, sizeof(values) / sizeof(values[0]));
}

bool work_item_source_valid(const char *value)
{
    static const char *const values[] = {
        "user", "log", "test", "github_issue", "heartbeat", "review",
    };
    return str_in_set(value, values, sizeof(values) / sizeof(values[0]));
}

bool work_item_status_valid(const char *value)
{
    static const char *const values[] = {
        "new", "triaged", "needs_info", "accepted", "planned", "fixing", "done", "rejected",
    };
    return str_in_set(value, values, sizeof(values) / sizeof(values[0]));
}

bool work_item_priority_valid(const char *value)
{
    static const char *const values[] = {"P0", "P1", "P2", "P3"};
    return str_in_set(value, values, sizeof(values) / sizeof(values[0]));
}

static void utc_now(char *buf, size_t size)
{
    if (!buf || size == 0) return;
    time_t now = time(NULL);
    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &tm_now);
}

static void local_date(char *buf, size_t size)
{
    if (!buf || size == 0) return;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, size, "%Y%m%d", &tm_now);
}

static const char *json_string_or_default(const cJSON *obj, const char *key, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItem((cJSON *)obj, key);
    const char *value = cJSON_IsString((cJSON *)item) ? item->valuestring : NULL;
    return value ? value : fallback;
}

static daima_err_t add_validated_string(cJSON *dst,
                                        const cJSON *src,
                                        const char *key,
                                        const char *fallback,
                                        bool (*validator)(const char *))
{
    const char *value = json_string_or_default(src, key, fallback);
    if (!validator(value)) {
        return DAIMA_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(dst, key, value);
    return DAIMA_OK;
}

static void add_plain_string(cJSON *dst, const cJSON *src, const char *key)
{
    cJSON_AddStringToObject(dst, key, json_string_or_default(src, key, ""));
}

static cJSON *normalized_evidence(const cJSON *input)
{
    cJSON *evidence_in = cJSON_GetObjectItem((cJSON *)input, "evidence");
    cJSON *evidence = cJSON_CreateObject();
    if (!evidence) return NULL;

    const char *session_id = "";
    const char *issue_url = "";
    if (evidence_in && cJSON_IsObject(evidence_in)) {
        session_id = json_string_or_default(evidence_in, "session_id", "");
        issue_url = json_string_or_default(evidence_in, "issue_url", "");
    }
    cJSON_AddStringToObject(evidence, "session_id", session_id);
    cJSON_AddStringToObject(evidence, "issue_url", issue_url);

    const char *array_keys[] = {"logs", "files", "commands"};
    for (size_t i = 0; i < sizeof(array_keys) / sizeof(array_keys[0]); i++) {
        cJSON *src_arr = evidence_in && cJSON_IsObject(evidence_in)
                             ? cJSON_GetObjectItem(evidence_in, array_keys[i])
                             : NULL;
        if (src_arr && cJSON_IsArray(src_arr)) {
            cJSON_AddItemToObject(evidence, array_keys[i], cJSON_Duplicate(src_arr, true));
        } else {
            cJSON_AddItemToObject(evidence, array_keys[i], cJSON_CreateArray());
        }
    }
    return evidence;
}

static daima_err_t normalize_new_item(const cJSON *input, const char *id, const char *now, cJSON **out_item)
{
    if (!input || !id || !now || !out_item || !cJSON_IsObject((cJSON *)input)) {
        return DAIMA_ERR_INVALID_ARG;
    }

    const char *title = json_string_or_default(input, "title", "");
    if (!title[0]) {
        return DAIMA_ERR_INVALID_ARG;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) return DAIMA_ERR_NO_MEM;

    cJSON_AddStringToObject(item, "id", id);
    if (add_validated_string(item, input, "type", "improvement", work_item_type_valid) != DAIMA_OK ||
        add_validated_string(item, input, "source", "user", work_item_source_valid) != DAIMA_OK) {
        cJSON_Delete(item);
        return DAIMA_ERR_INVALID_ARG;
    }
    add_plain_string(item, input, "title");
    add_plain_string(item, input, "description");
    add_plain_string(item, input, "expected");
    add_plain_string(item, input, "actual");

    cJSON *evidence = normalized_evidence(input);
    if (!evidence) {
        cJSON_Delete(item);
        return DAIMA_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(item, "evidence", evidence);

    if (add_validated_string(item, input, "status", "new", work_item_status_valid) != DAIMA_OK ||
        add_validated_string(item, input, "priority", "P2", work_item_priority_valid) != DAIMA_OK) {
        cJSON_Delete(item);
        return DAIMA_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(item, "created_at", now);
    cJSON_AddStringToObject(item, "updated_at", now);
    *out_item = item;
    return DAIMA_OK;
}

static daima_err_t append_item_line(const cJSON *item)
{
    char *line = cJSON_PrintUnformatted((cJSON *)item);
    if (!line) return DAIMA_ERR_NO_MEM;

    daima_fs_ensure_dir(daima_path_memory_dir());
    FILE *f = fopen(daima_path_work_items_file(), "a");
    if (!f) {
        free(line);
        return DAIMA_FAIL;
    }
    int ok = fprintf(f, "%s\n", line) > 0;
    fclose(f);
    free(line);
    return ok ? DAIMA_OK : DAIMA_FAIL;
}

void work_item_list_free(work_item_list_t *list)
{
    if (!list) return;
    cJSON_Delete(list->items);
    list->items = NULL;
    list->invalid_lines = 0;
}

daima_err_t work_item_store_load(work_item_list_t *out)
{
    if (!out) return DAIMA_ERR_INVALID_ARG;
    out->items = cJSON_CreateArray();
    out->invalid_lines = 0;
    if (!out->items) return DAIMA_ERR_NO_MEM;

    FILE *f = fopen(daima_path_work_items_file(), "r");
    if (!f) return DAIMA_OK;

    char line[WORK_ITEM_LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        cJSON *item = cJSON_Parse(line);
        if (!item || !cJSON_IsObject(item)) {
            cJSON_Delete(item);
            out->invalid_lines++;
            continue;
        }
        cJSON_AddItemToArray(out->items, item);
    }
    fclose(f);
    return DAIMA_OK;
}

static const char *find_duplicate(const cJSON *items, const char *title, const char *type)
{
    if (!title || !title[0]) return NULL;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, (cJSON *)items) {
        const char *exist_title = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)item, "title"));
        if (!exist_title || strcmp(exist_title, title) != 0) continue;
        const char *exist_type = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)item, "type"));
        if (!exist_type || strcmp(exist_type, type) != 0) continue;
        return cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)item, "id"));
    }
    return NULL;
}

static int next_sequence_for_date(const cJSON *items, const char *date)
{
    int max_seq = 0;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "WI-%s-", date);
    size_t prefix_len = strlen(prefix);

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, (cJSON *)items) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)item, "id"));
        if (!id || strncmp(id, prefix, prefix_len) != 0) continue;
        int seq = atoi(id + prefix_len);
        if (seq > max_seq) max_seq = seq;
    }
    return max_seq + 1;
}

daima_err_t work_item_store_add(const cJSON *input, cJSON **out_item)
{
    if (!input || !out_item) return DAIMA_ERR_INVALID_ARG;
    *out_item = NULL;

    work_item_list_t list = {0};
    daima_err_t err = work_item_store_load(&list);
    if (err != DAIMA_OK) return err;

    char date[16];
    char id[32];
    char now[32];
    local_date(date, sizeof(date));
    snprintf(id, sizeof(id), "WI-%s-%03d", date, next_sequence_for_date(list.items, date));
    utc_now(now, sizeof(now));

    const char *title = json_string_or_default(input, "title", "");
    const char *type_val = json_string_or_default(input, "type", "improvement");
    const char *dup_id = find_duplicate(list.items, title, type_val);

    cJSON *item = NULL;
    err = normalize_new_item(input, id, now, &item);
    if (err == DAIMA_OK && dup_id) {
        const char *orig_desc = cJSON_GetStringValue(cJSON_GetObjectItem(item, "description"));
        char marked_desc[2048];
        snprintf(marked_desc, sizeof(marked_desc),
                 "%s%s%s",
                 orig_desc && orig_desc[0] ? orig_desc : "",
                 orig_desc && orig_desc[0] ? " " : "",
                 dup_id);
        cJSON_ReplaceItemInObject(item, "description", cJSON_CreateString(marked_desc));
        cJSON_AddStringToObject(item, "duplicate_of", dup_id);
    }
    if (err == DAIMA_OK) {
        err = append_item_line(item);
    }
    if (err == DAIMA_OK) {
        *out_item = item;
    } else {
        cJSON_Delete(item);
    }
    work_item_list_free(&list);
    return err;
}

static daima_err_t apply_update_fields(cJSON *item, const cJSON *input, const char *now)
{
    const char *string_keys[] = {"title", "description", "expected", "actual"};
    for (size_t i = 0; i < sizeof(string_keys) / sizeof(string_keys[0]); i++) {
        cJSON *value = cJSON_GetObjectItem((cJSON *)input, string_keys[i]);
        if (value && cJSON_IsString(value)) {
            cJSON_ReplaceItemInObject(item, string_keys[i], cJSON_CreateString(value->valuestring));
        }
    }

    struct {
        const char *key;
        bool (*validator)(const char *);
    } enum_keys[] = {
        {"type", work_item_type_valid},
        {"source", work_item_source_valid},
        {"status", work_item_status_valid},
        {"priority", work_item_priority_valid},
    };
    for (size_t i = 0; i < sizeof(enum_keys) / sizeof(enum_keys[0]); i++) {
        cJSON *value = cJSON_GetObjectItem((cJSON *)input, enum_keys[i].key);
        if (!value) continue;
        if (!cJSON_IsString(value) || !enum_keys[i].validator(value->valuestring)) {
            return DAIMA_ERR_INVALID_ARG;
        }
        cJSON_ReplaceItemInObject(item, enum_keys[i].key, cJSON_CreateString(value->valuestring));
    }

    cJSON *evidence_in = cJSON_GetObjectItem((cJSON *)input, "evidence");
    if (evidence_in) {
        if (!cJSON_IsObject(evidence_in)) return DAIMA_ERR_INVALID_ARG;
        cJSON *evidence = normalized_evidence(input);
        if (!evidence) return DAIMA_ERR_NO_MEM;
        cJSON_ReplaceItemInObject(item, "evidence", evidence);
    }
    cJSON_ReplaceItemInObject(item, "updated_at", cJSON_CreateString(now));
    return DAIMA_OK;
}

static daima_err_t rewrite_items(const cJSON *items)
{
    daima_fs_ensure_dir(daima_path_memory_dir());
    FILE *f = fopen(daima_path_work_items_file(), "w");
    if (!f) return DAIMA_FAIL;

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, (cJSON *)items) {
        char *line = cJSON_PrintUnformatted((cJSON *)item);
        if (!line) {
            fclose(f);
            return DAIMA_ERR_NO_MEM;
        }
        fprintf(f, "%s\n", line);
        free(line);
    }
    fclose(f);
    return DAIMA_OK;
}

daima_err_t work_item_store_update(const char *id, const cJSON *input, cJSON **out_item)
{
    if (!id || !id[0] || !input || !out_item) return DAIMA_ERR_INVALID_ARG;
    *out_item = NULL;

    work_item_list_t list = {0};
    daima_err_t err = work_item_store_load(&list);
    if (err != DAIMA_OK) return err;

    cJSON *found = NULL;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, list.items) {
        const char *item_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
        if (item_id && strcmp(item_id, id) == 0) {
            found = item;
            break;
        }
    }
    if (!found) {
        work_item_list_free(&list);
        return DAIMA_ERR_NOT_FOUND;
    }

    char now[32];
    utc_now(now, sizeof(now));
    err = apply_update_fields(found, input, now);
    if (err == DAIMA_OK) {
        err = rewrite_items(list.items);
    }
    if (err == DAIMA_OK) {
        *out_item = cJSON_Duplicate(found, true);
        if (!*out_item) err = DAIMA_ERR_NO_MEM;
    }
    work_item_list_free(&list);
    return err;
}

daima_err_t work_item_store_batch_update(const cJSON *ids, const char *status, int *out_count)
{
    if (!ids || !cJSON_IsArray(ids) || !status || !work_item_status_valid(status) || !out_count) {
        return DAIMA_ERR_INVALID_ARG;
    }
    *out_count = 0;

    work_item_list_t list = {0};
    daima_err_t err = work_item_store_load(&list);
    if (err != DAIMA_OK) return err;

    char now[32];
    utc_now(now, sizeof(now));
    int updated = 0;

    const cJSON *id_item = NULL;
    cJSON_ArrayForEach(id_item, (cJSON *)ids) {
        const char *target_id = cJSON_IsString((cJSON *)id_item) ? id_item->valuestring : NULL;
        if (!target_id) continue;

        cJSON *item = NULL;
        cJSON_ArrayForEach(item, list.items) {
            const char *item_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
            if (item_id && strcmp(item_id, target_id) == 0) {
                cJSON_ReplaceItemInObject(item, "status", cJSON_CreateString(status));
                cJSON_ReplaceItemInObject(item, "updated_at", cJSON_CreateString(now));
                updated++;
                break;
            }
        }
    }

    if (updated > 0) {
        err = rewrite_items(list.items);
    }
    *out_count = updated;
    work_item_list_free(&list);
    return err;
}

daima_err_t work_item_store_collect(const char *type,
                                    const char *source,
                                    const char *title,
                                    const char *description)
{
    if (!type || !source || !title) return DAIMA_ERR_INVALID_ARG;
    if (!work_item_type_valid(type) || !work_item_source_valid(source)) return DAIMA_ERR_INVALID_ARG;

    cJSON *input = cJSON_CreateObject();
    if (!input) return DAIMA_ERR_NO_MEM;
    cJSON_AddStringToObject(input, "type", type);
    cJSON_AddStringToObject(input, "source", source);
    cJSON_AddStringToObject(input, "title", title);
    cJSON_AddStringToObject(input, "description", description ? description : "");
    cJSON_AddStringToObject(input, "status", "new");

    cJSON *item = NULL;
    daima_err_t err = work_item_store_add(input, &item);
    cJSON_Delete(item);
    cJSON_Delete(input);
    return err;
}
