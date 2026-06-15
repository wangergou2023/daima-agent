#include "drivers/skill/skill_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#define SKILL_TOOL_NAME_LEN 64
#define SKILL_TOOL_DESCRIPTION_LEN 256
#define SKILL_TOOL_SCHEMA_LEN 2048

typedef struct {
    char name[SKILL_TOOL_NAME_LEN];
    char description[SKILL_TOOL_DESCRIPTION_LEN];
    char input_schema_json[SKILL_TOOL_SCHEMA_LEN];
} skill_tool_storage_t;

typedef struct {
    skill_tool_bundle_t bundle;
    skill_tool_storage_t storage[SKILL_TOOLS_MAX];
    bool loaded;
} skill_tool_bundle_slot_t;

static skill_tool_bundle_slot_t s_bundles[SKILL_TOOLS_MAX];

static err_t skill_tool_stub_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    if (!output || output_size == 0) {
        return ERR_INVALID_ARG;
    }
    snprintf(output, output_size, "skill tool not yet implemented");
    return 0;
}

static bool file_exists_regular(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static skill_tool_bundle_slot_t *find_bundle(const char *skill_name)
{
    if (!skill_name || !skill_name[0]) {
        return NULL;
    }
    for (int i = 0; i < SKILL_TOOLS_MAX; i++) {
        if (s_bundles[i].bundle.active && strcmp(s_bundles[i].bundle.skill_name, skill_name) == 0) {
            return &s_bundles[i];
        }
    }
    return NULL;
}

static skill_tool_bundle_slot_t *find_loaded_bundle(const char *skill_name)
{
    if (!skill_name || !skill_name[0]) {
        return NULL;
    }
    for (int i = 0; i < SKILL_TOOLS_MAX; i++) {
        if (s_bundles[i].loaded && strcmp(s_bundles[i].bundle.skill_name, skill_name) == 0) {
            return &s_bundles[i];
        }
    }
    return NULL;
}

static skill_tool_bundle_slot_t *alloc_bundle(const char *skill_name)
{
    for (int i = 0; i < SKILL_TOOLS_MAX; i++) {
        if (!s_bundles[i].bundle.active) {
            memset(&s_bundles[i], 0, sizeof(s_bundles[i]));
            s_bundles[i].bundle.active = true;
            s_bundles[i].loaded = true;
            strscpy(s_bundles[i].bundle.skill_name, skill_name, sizeof(s_bundles[i].bundle.skill_name));
            return &s_bundles[i];
        }
    }
    return NULL;
}

static err_t activate_loaded_bundle(skill_tool_bundle_slot_t *slot)
{
    if (!slot) {
        return ERR_INVALID_ARG;
    }
    if (slot->bundle.active) {
        return 0;
    }
    for (int i = 0; i < slot->bundle.tool_count; i++) {
        err_t err = tool_registry_register_dynamic(&slot->bundle.tools[i]);
        if (err != 0) {
            for (int j = 0; j < i; j++) {
                tool_registry_unregister_dynamic(slot->bundle.tools[j].name);
            }
            return err;
        }
    }
    slot->bundle.active = true;
    return 0;
}

static char *read_file_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = kzalloc((size_t)len + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static err_t init_tool_from_json(skill_tool_bundle_slot_t *slot, cJSON *item)
{
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
    const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(item, "description"));
    const char *schema = cJSON_GetStringValue(cJSON_GetObjectItem(item, "input_schema_json"));
    if (!name || !name[0] || !description || !schema) {
        return ERR_INVALID_ARG;
    }

    int idx = slot->bundle.tool_count;
    if (idx >= SKILL_TOOLS_MAX) {
        return ERR_NO_MEM;
    }

    skill_tool_storage_t *storage = &slot->storage[idx];
    strscpy(storage->name, name, sizeof(storage->name));
    strscpy(storage->description, description, sizeof(storage->description));
    strscpy(storage->input_schema_json, schema, sizeof(storage->input_schema_json));

    struct tool tool = {
        .name = storage->name,
        .description = storage->description,
        .input_schema_json = storage->input_schema_json,
        .execute = skill_tool_stub_execute,
    };
    err_t err = tool_registry_register_dynamic(&tool);
    if (err != 0) {
        return err;
    }

    slot->bundle.tools[idx] = tool;
    slot->bundle.tool_count++;
    return 0;
}

err_t skill_tools_register(const char *skill_name, const char *skill_dir)
{
    if (!skill_name || !skill_name[0] || !skill_dir || !skill_dir[0]) {
        return ERR_INVALID_ARG;
    }
    skill_tool_bundle_slot_t *loaded = find_loaded_bundle(skill_name);
    if (loaded) {
        return activate_loaded_bundle(loaded);
    }

    char tools_path[512];
    snprintf(tools_path, sizeof(tools_path), "%s/TOOLS.json", skill_dir);
    if (!file_exists_regular(tools_path)) {
        return 0;
    }

    char *json = read_file_all(tools_path);
    if (!json) {
        return ERR_FAIL;
    }

    cJSON *root = cJSON_Parse(json);
    kfree(json);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ERR_INVALID_ARG;
    }

    skill_tool_bundle_slot_t *slot = alloc_bundle(skill_name);
    if (!slot) {
        cJSON_Delete(root);
        return ERR_NO_MEM;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        err_t err = init_tool_from_json(slot, item);
        if (err != 0) {
            skill_tools_unregister(skill_name);
            cJSON_Delete(root);
            return err;
        }
    }

    cJSON_Delete(root);
    pr_info("Registered %d skill-scoped tools for %s", slot->bundle.tool_count, skill_name);
    return 0;
}

err_t skill_tools_unregister(const char *skill_name)
{
    skill_tool_bundle_slot_t *slot = find_bundle(skill_name);
    if (!slot) {
        return 0;
    }

    for (int i = 0; i < slot->bundle.tool_count; i++) {
        tool_registry_unregister_dynamic(slot->bundle.tools[i].name);
    }
    pr_info("Unregistered skill-scoped tools for %s", slot->bundle.skill_name);
    slot->bundle.active = false;
    return 0;
}

void skill_tools_unregister_all(void)
{
    char names[SKILL_TOOLS_MAX][64];
    int count = 0;
    for (int i = 0; i < SKILL_TOOLS_MAX; i++) {
        if (s_bundles[i].loaded && s_bundles[i].bundle.active) {
            strscpy(names[count++], s_bundles[i].bundle.skill_name, sizeof(names[0]));
        }
    }
    for (int i = 0; i < count; i++) {
        skill_tools_unregister(names[i]);
    }
}
