#include "drivers/channel/feishu/feishu_targets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "paths.h"
#include "runtime.h"
#include "autoconf.h"
#include "cJSON.h"
#include "linux/printk.h"
#include "json_helpers.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#define FEISHU_TARGETS_MAX 16

static void targets_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/feishu_targets.json", daima_path_cache_dir());
}

static cJSON *load_targets_root(void)
{
    char path[DAIMA_BUF_SMALL];
    targets_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddArrayToObject(root, "targets");
        return root;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 32 * 1024) {
        fclose(f);
        return NULL;
    }

    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, (size_t)size, f);
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    kfree(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!cJSON_GetObjectItem(root, "targets")) {
        cJSON_AddArrayToObject(root, "targets");
    }
    return root;
}

static bool save_targets_root(cJSON *root)
{
    if (!root) return false;

    char *json = cJSON_Print(root);
    if (!json) return false;

    char path[DAIMA_BUF_SMALL];
    targets_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) {
        pr_warn("Cannot write %s", path);
        kfree(json);
        return false;
    }
    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, f);
    fclose(f);
    kfree(json);
    return written == len;
}

bool feishu_targets_record(const char *route_id,
                           const char *chat_id,
                           const char *chat_type,
                           const char *sender_id)
{
    if (!route_id || !route_id[0]) {
        return false;
    }

    cJSON *root = load_targets_root();
    if (!root) return false;
    cJSON *arr = cJSON_GetObjectItem(root, "targets");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_DeleteItemFromObject(root, "targets");
        arr = cJSON_AddArrayToObject(root, "targets");
    }

    time_t now = time(NULL);
    cJSON *existing = NULL;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        const char *item_route_id = json_string(item, "route_id");
        if (item_route_id && strcmp(item_route_id, route_id) == 0) {
            existing = item;
            break;
        }
    }

    if (!existing) {
        existing = cJSON_CreateObject();
        if (!existing) {
            cJSON_Delete(root);
            return false;
        }
        cJSON_AddItemToArray(arr, existing);
    }

    cJSON_DeleteItemFromObject(existing, "route_id");
    cJSON_AddStringToObject(existing, "route_id", route_id);
    cJSON_DeleteItemFromObject(existing, "chat_id");
    cJSON_AddStringToObject(existing, "chat_id", chat_id ? chat_id : "");
    cJSON_DeleteItemFromObject(existing, "chat_type");
    cJSON_AddStringToObject(existing, "chat_type", chat_type ? chat_type : "");
    cJSON_DeleteItemFromObject(existing, "sender_id");
    cJSON_AddStringToObject(existing, "sender_id", sender_id ? sender_id : "");
    cJSON_DeleteItemFromObject(existing, "last_seen");
    cJSON_AddNumberToObject(existing, "last_seen", (double)now);

    while (cJSON_GetArraySize(arr) > FEISHU_TARGETS_MAX) {
        int oldest_idx = 0;
        double oldest = 0;
        int idx = 0;
        cJSON *candidate = NULL;
        cJSON_ArrayForEach(candidate, arr) {
            cJSON *seen = cJSON_GetObjectItem(candidate, "last_seen");
            double value = cJSON_IsNumber(seen) ? seen->valuedouble : 0;
            if (idx == 0 || value < oldest) {
                oldest = value;
                oldest_idx = idx;
            }
            idx++;
        }
        cJSON_DeleteItemFromArray(arr, oldest_idx);
    }

    bool ok = save_targets_root(root);
    cJSON_Delete(root);
    if (ok) {
        pr_info("Recorded Feishu target: %s", route_id);
    }
    return ok;
}

bool feishu_targets_get_default(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    const char *configured = runtime_config_get_feishu_default_chat_id();
    if (configured && configured[0]) {
        strscpy(out, configured, out_size);
        return true;
    }

    cJSON *root = load_targets_root();
    if (!root) return false;
    cJSON *arr = cJSON_GetObjectItem(root, "targets");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return false;
    }

    const char *best_id = NULL;
    double best_seen = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        const char *route_id = json_string(item, "route_id");
        cJSON *seen = cJSON_GetObjectItem(item, "last_seen");
        double last_seen = cJSON_IsNumber(seen) ? seen->valuedouble : 0;
        if (route_id && route_id[0] && (!best_id || last_seen > best_seen)) {
            best_id = route_id;
            best_seen = last_seen;
        }
    }

    if (best_id) {
        strscpy(out, best_id, out_size);
    }
    cJSON_Delete(root);
    return best_id != NULL;
}
