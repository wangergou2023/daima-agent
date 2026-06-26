/* 运行时配置写回。 */

#include "runtime.h"
#include "paths.h"
#include "runtime_defaults.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cjson.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"

static bool terminal_security_level_valid(const char *level)
{
    return level &&
           (strcmp(level, "plan") == 0 ||
            strcmp(level, "build") == 0);
}

static char *read_config_text(void)
{
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());
    FILE *f = fopen(cfg_path, "rb");
    char *buf = NULL;
    long size = 0;
    size_t n = 0;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || size > 128 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = kzalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static err_t write_config_json_atomic(cJSON *root)
{
    if (!root) {
        return ERR_INVALID_ARG;
    }

    char *text = cJSON_Print(root);
    if (!text) {
        return ERR_NO_MEM;
    }

    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/config.json", path_config_dir());
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cfg_path);
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        kfree(text);
        return ERR_FAIL;
    }

    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len && fwrite("\n", 1, 1, f) == 1;
    if (fclose(f) != 0) {
        ok = false;
    }
    kfree(text);
    if (!ok) {
        unlink(tmp_path);
        return ERR_FAIL;
    }
    if (rename(tmp_path, cfg_path) != 0) {
        unlink(tmp_path);
        return ERR_FAIL;
    }
    return 0;
}

err_t runtime_config_set_terminal_security_level(const char *level)
{
    if (!terminal_security_level_valid(level)) {
        return ERR_INVALID_ARG;
    }

    char *text = read_config_text();
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    kfree(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root) {
        return ERR_NO_MEM;
    }

    cJSON *common = cJSON_GetObjectItemCaseSensitive(root, "common");
    if (!common || !cJSON_IsObject(common)) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "common");
        common = cJSON_AddObjectToObject(root, "common");
    }
    if (!common) {
        cJSON_Delete(root);
        return ERR_NO_MEM;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(common, "terminal_security_level");
    cJSON_AddStringToObject(common, "terminal_security_level", level);

    err_t err = write_config_json_atomic(root);
    cJSON_Delete(root);
    if (err != 0) {
        return err;
    }

    strscpy(s_cfg.terminal_security_level, level, sizeof(s_cfg.terminal_security_level));
    pr_info("Terminal security level set to %s", level);
    return 0;
}
