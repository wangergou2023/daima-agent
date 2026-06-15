#include "work_item.h"

#include "fs.h"
#include "paths.h"
#include "json_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "linux/slab.h"

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

static err_t add_validated_string(cJSON *dst,
                                        const cJSON *src,
                                        const char *key,
                                        const char *fallback,
                                        bool (*validator)(const char *))
{
    const char *value = json_string_or_default(src, key, fallback);
    if (!validator(value)) {
        return ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(dst, key, value);
    return 0;
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

    const char *array_keys[] = {"logs", "files", "commands", "tool_calls"};
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

static void add_optional_string(cJSON *dst, const cJSON *src, const char *key)
{
    const char *value = json_string_or_default(src, key, "");
    if (value && value[0]) {
        cJSON_AddStringToObject(dst, key, value);
    }
}

static err_t normalize_new_item(const cJSON *input, const char *id, const char *now, cJSON **out_item)
{
    if (!input || !id || !now || !out_item || !cJSON_IsObject((cJSON *)input)) {
        return ERR_INVALID_ARG;
    }

    const char *title = json_string_or_default(input, "title", "");
    if (!title[0]) {
        return ERR_INVALID_ARG;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) return ERR_NO_MEM;

    cJSON_AddStringToObject(item, "id", id);
    if (add_validated_string(item, input, "type", "improvement", work_item_type_valid) != 0 ||
        add_validated_string(item, input, "source", "user", work_item_source_valid) != 0) {
        cJSON_Delete(item);
        return ERR_INVALID_ARG;
    }
    add_plain_string(item, input, "title");
    add_plain_string(item, input, "description");
    add_plain_string(item, input, "expected");
    add_plain_string(item, input, "actual");

    cJSON *evidence = normalized_evidence(input);
    if (!evidence) {
        cJSON_Delete(item);
        return ERR_NO_MEM;
    }
    cJSON_AddItemToObject(item, "evidence", evidence);

    if (add_validated_string(item, input, "status", "new", work_item_status_valid) != 0 ||
        add_validated_string(item, input, "priority", "P2", work_item_priority_valid) != 0) {
        cJSON_Delete(item);
        return ERR_INVALID_ARG;
    }
    add_optional_string(item, input, "error_signature");
    cJSON *occurrences = cJSON_GetObjectItem((cJSON *)input, "occurrences");
    cJSON_AddNumberToObject(item, "occurrences", cJSON_IsNumber(occurrences) && occurrences->valueint > 0 ? occurrences->valueint : 1);
    cJSON_AddStringToObject(item, "first_seen_at", json_string_or_default(input, "first_seen_at", now));
    cJSON_AddStringToObject(item, "last_seen_at", json_string_or_default(input, "last_seen_at", now));
    cJSON_AddStringToObject(item, "created_at", now);
    cJSON_AddStringToObject(item, "updated_at", now);
    *out_item = item;
    return 0;
}

static err_t append_item_line(const cJSON *item)
{
    char *line = cJSON_PrintUnformatted((cJSON *)item);
    if (!line) return ERR_NO_MEM;

    fs_ensure_dir(path_memory_dir());
    FILE *f = fopen(path_work_items_file(), "a");
    if (!f) {
        kfree(line);
        return ERR_FAIL;
    }
    int ok = fprintf(f, "%s\n", line) > 0;
    fclose(f);
    kfree(line);
    return ok ? 0 : ERR_FAIL;
}

void work_item_list_free(work_item_list_t *list)
{
    if (!list) return;
    cJSON_Delete(list->items);
    list->items = NULL;
    list->invalid_lines = 0;
}

err_t work_item_store_load(work_item_list_t *out)
{
    if (!out) return ERR_INVALID_ARG;
    out->items = cJSON_CreateArray();
    out->invalid_lines = 0;
    if (!out->items) return ERR_NO_MEM;

    FILE *f = fopen(path_work_items_file(), "r");
    if (!f) return 0;

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
    return 0;
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

static cJSON *find_active_by_signature(cJSON *items, const char *signature)
{
    if (!items || !signature || !signature[0]) return NULL;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        const char *item_sig = cJSON_GetStringValue(cJSON_GetObjectItem(item, "error_signature"));
        if (!item_sig || strcmp(item_sig, signature) != 0) continue;
        const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(item, "status"));
        if (status && (strcmp(status, "done") == 0 || strcmp(status, "rejected") == 0)) {
            continue;
        }
        return item;
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

err_t work_item_store_add(const cJSON *input, cJSON **out_item)
{
    if (!input || !out_item) return ERR_INVALID_ARG;
    *out_item = NULL;

    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

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
    if (err == 0 && dup_id) {
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
    if (err == 0) {
        err = append_item_line(item);
    }
    if (err == 0) {
        *out_item = item;
    } else {
        cJSON_Delete(item);
    }
    work_item_list_free(&list);
    return err;
}

static err_t apply_update_fields(cJSON *item, const cJSON *input, const char *now)
{
    const char *string_keys[] = {"title", "description", "expected", "actual", "error_signature", "first_seen_at", "last_seen_at"};
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
            return ERR_INVALID_ARG;
        }
        cJSON_ReplaceItemInObject(item, enum_keys[i].key, cJSON_CreateString(value->valuestring));
    }

    cJSON *evidence_in = cJSON_GetObjectItem((cJSON *)input, "evidence");
    if (evidence_in) {
        if (!cJSON_IsObject(evidence_in)) return ERR_INVALID_ARG;
        cJSON *evidence = normalized_evidence(input);
        if (!evidence) return ERR_NO_MEM;
        cJSON_ReplaceItemInObject(item, "evidence", evidence);
    }
    cJSON *occurrences = cJSON_GetObjectItem((cJSON *)input, "occurrences");
    if (occurrences) {
        if (!cJSON_IsNumber(occurrences)) return ERR_INVALID_ARG;
        cJSON_ReplaceItemInObject(item, "occurrences", cJSON_CreateNumber(occurrences->valueint));
    }
    cJSON_ReplaceItemInObject(item, "updated_at", cJSON_CreateString(now));
    return 0;
}

static cJSON *ensure_evidence_object(cJSON *item)
{
    cJSON *evidence = cJSON_GetObjectItem(item, "evidence");
    if (evidence && cJSON_IsObject(evidence)) {
        return evidence;
    }
    evidence = cJSON_CreateObject();
    if (!evidence) return NULL;
    cJSON_ReplaceItemInObject(item, "evidence", evidence);
    return evidence;
}

static cJSON *ensure_evidence_array(cJSON *evidence, const char *key)
{
    cJSON *arr = cJSON_GetObjectItem(evidence, key);
    if (arr && cJSON_IsArray(arr)) {
        return arr;
    }
    arr = cJSON_CreateArray();
    if (!arr) return NULL;
    cJSON_ReplaceItemInObject(evidence, key, arr);
    return arr;
}

static void append_limited_array_items(cJSON *dst, const cJSON *src, int limit)
{
    if (!dst || !src || !cJSON_IsArray((cJSON *)dst) || !cJSON_IsArray((cJSON *)src)) {
        return;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, (cJSON *)src) {
        if (cJSON_GetArraySize(dst) >= limit) {
            break;
        }
        cJSON *dup = cJSON_Duplicate((cJSON *)entry, true);
        if (dup) {
            cJSON_AddItemToArray(dst, dup);
        }
    }
}

static void merge_evidence(cJSON *existing, const cJSON *incoming)
{
    if (!existing || !incoming || !cJSON_IsObject((cJSON *)incoming)) {
        return;
    }
    cJSON *evidence = ensure_evidence_object(existing);
    if (!evidence) return;

    cJSON *session_in = cJSON_GetObjectItem((cJSON *)incoming, "session_id");
    if (session_in && cJSON_IsString(session_in) && session_in->valuestring[0]) {
        cJSON_ReplaceItemInObject(evidence, "session_id", cJSON_CreateString(session_in->valuestring));
    }

    const char *array_keys[] = {"logs", "files", "commands", "tool_calls"};
    for (size_t i = 0; i < sizeof(array_keys) / sizeof(array_keys[0]); i++) {
        cJSON *src_arr = cJSON_GetObjectItem((cJSON *)incoming, array_keys[i]);
        if (!src_arr || !cJSON_IsArray(src_arr)) continue;
        cJSON *dst_arr = ensure_evidence_array(evidence, array_keys[i]);
        append_limited_array_items(dst_arr, src_arr, 10);
    }
}

static err_t rewrite_items(const cJSON *items)
{
    fs_ensure_dir(path_memory_dir());
    FILE *f = fopen(path_work_items_file(), "w");
    if (!f) return ERR_FAIL;

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, (cJSON *)items) {
        char *line = cJSON_PrintUnformatted((cJSON *)item);
        if (!line) {
            fclose(f);
            return ERR_NO_MEM;
        }
        fprintf(f, "%s\n", line);
        kfree(line);
    }
    fclose(f);
    return 0;
}

err_t work_item_store_update(const char *id, const cJSON *input, cJSON **out_item)
{
    if (!id || !id[0] || !input || !out_item) return ERR_INVALID_ARG;
    *out_item = NULL;

    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

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
        return ERR_NOT_FOUND;
    }

    char now[32];
    utc_now(now, sizeof(now));
    err = apply_update_fields(found, input, now);
    if (err == 0) {
        err = rewrite_items(list.items);
    }
    if (err == 0) {
        *out_item = cJSON_Duplicate(found, true);
        if (!*out_item) err = ERR_NO_MEM;
    }
    work_item_list_free(&list);
    return err;
}

err_t work_item_store_batch_update(const cJSON *ids, const char *status, int *out_count)
{
    if (!ids || !cJSON_IsArray(ids) || !status || !work_item_status_valid(status) || !out_count) {
        return ERR_INVALID_ARG;
    }
    *out_count = 0;

    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

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

err_t work_item_store_collect(const char *type,
                                    const char *source,
                                    const char *title,
                                    const char *description)
{
    if (!type || !source || !title) return ERR_INVALID_ARG;
    if (!work_item_type_valid(type) || !work_item_source_valid(source)) return ERR_INVALID_ARG;

    cJSON *input = cJSON_CreateObject();
    if (!input) return ERR_NO_MEM;
    cJSON_AddStringToObject(input, "type", type);
    cJSON_AddStringToObject(input, "source", source);
    cJSON_AddStringToObject(input, "title", title);
    cJSON_AddStringToObject(input, "description", description ? description : "");
    cJSON_AddStringToObject(input, "status", "new");

    cJSON *item = NULL;
    err_t err = work_item_store_add(input, &item);
    cJSON_Delete(item);
    cJSON_Delete(input);
    return err;
}

err_t work_item_store_collect_structured(const cJSON *input, cJSON **out_item)
{
    if (!input || !cJSON_IsObject((cJSON *)input) || !out_item) {
        return ERR_INVALID_ARG;
    }
    *out_item = NULL;

    const char *signature = json_string_or_default(input, "error_signature", "");
    if (!signature[0]) {
        return work_item_store_add(input, out_item);
    }

    work_item_list_t list = {0};
    err_t err = work_item_store_load(&list);
    if (err != 0) return err;

    cJSON *existing = find_active_by_signature(list.items, signature);
    if (!existing) {
        work_item_list_free(&list);
        return work_item_store_add(input, out_item);
    }

    char now[32];
    utc_now(now, sizeof(now));
    cJSON *occ = cJSON_GetObjectItem(existing, "occurrences");
    int next_occ = cJSON_IsNumber(occ) ? occ->valueint + 1 : 2;
    cJSON_ReplaceItemInObject(existing, "occurrences", cJSON_CreateNumber(next_occ));
    cJSON_ReplaceItemInObject(existing, "last_seen_at", cJSON_CreateString(now));
    cJSON_ReplaceItemInObject(existing, "updated_at", cJSON_CreateString(now));

    cJSON *incoming_evidence = cJSON_GetObjectItem((cJSON *)input, "evidence");
    merge_evidence(existing, incoming_evidence);

    err = rewrite_items(list.items);
    if (err == 0) {
        *out_item = cJSON_Duplicate(existing, true);
        if (!*out_item) err = ERR_NO_MEM;
    }
    work_item_list_free(&list);
    return err;
}
