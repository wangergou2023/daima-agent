#include "paths.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "linux/kernel.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *AGENT_HOME_ENV = "AGENT_HOME";
static const char *DEFAULT_HOME_NAME = ".agent-data";

typedef struct {
    int initialized;
    char home[PATH_MAX];
    char spiffs_base[PATH_MAX];
    char config_dir[PATH_MAX];
    char memory_dir[PATH_MAX];
    char session_dir[PATH_MAX];
    char cache_dir[PATH_MAX];
    char checkpoint_dir[PATH_MAX];
    char web_dir[PATH_MAX];
    char feishu_image_dir[PATH_MAX];
    char skills_dir[PATH_MAX];
    char workspace_dir[PATH_MAX];
    char runtime_config_file[PATH_MAX];
    char bootstrap_file[PATH_MAX];
    char identity_file[PATH_MAX];
    char soul_file[PATH_MAX];
    char user_file[PATH_MAX];
    char memory_file[PATH_MAX];
    char skill_review_queue_file[PATH_MAX];
    char todo_file[PATH_MAX];
    char work_items_file[PATH_MAX];
    char log_file[PATH_MAX];
    char web_index_file[PATH_MAX];
    char web_css_file[PATH_MAX];
    char web_js_file[PATH_MAX];
    char prompt_debug_file[PATH_MAX];
    char cron_file[PATH_MAX];
    char heartbeat_file[PATH_MAX];
    char ca_cert_file[PATH_MAX];
} paths_state_t;

static paths_state_t s_paths = {0};

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strscpy(dst, src ? src : "", dst_size);
}

static void join_path2(char *dst, size_t dst_size, const char *a, const char *b)
{
    if (!dst || dst_size == 0) return;
    if (!a || !a[0]) {
        safe_copy(dst, dst_size, b);
        return;
    }
    if (!b || !b[0]) {
        safe_copy(dst, dst_size, a);
        return;
    }
    if (a[strlen(a) - 1] == '/') {
        snprintf(dst, dst_size, "%s%s", a, b);
    } else {
        snprintf(dst, dst_size, "%s/%s", a, b);
    }
}

static bool dir_has_spiffs_data(const char *dir)
{
    if (!dir || !dir[0]) return false;
    char candidate[PATH_MAX];
    join_path2(candidate, sizeof(candidate), dir, "spiffs_data");
    return access(candidate, F_OK) == 0;
}

static bool get_executable_dir(char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;

    char exe_path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0 || n >= (ssize_t)sizeof(exe_path)) {
        return false;
    }
    exe_path[n] = '\0';

    char *slash = strrchr(exe_path, '/');
    if (!slash) {
        return false;
    }
    *slash = '\0';
    safe_copy(out, out_size, exe_path);
    return true;
}

static void dirname_inplace(char *path)
{
    if (!path || !path[0]) return;

    char *slash = strrchr(path, '/');
    if (!slash) {
        path[0] = '.';
        path[1] = '\0';
        return;
    }
    if (slash == path) {
        path[1] = '\0';
        return;
    }
    *slash = '\0';
}

static void detect_home_dir(char *out, size_t out_size)
{
    const char *env_home = getenv(AGENT_HOME_ENV);
    if (env_home && env_home[0]) {
        safe_copy(out, out_size, env_home);
        return;
    }

    const char *home_env = getenv("HOME");
    if (home_env && home_env[0]) {
        snprintf(out, out_size, "%s/%s", home_env, DEFAULT_HOME_NAME);
        return;
    }

    char exe_dir[PATH_MAX];
    if (get_executable_dir(exe_dir, sizeof(exe_dir))) {
        if (dir_has_spiffs_data(exe_dir)) {
            safe_copy(out, out_size, exe_dir);
            return;
        }

        char parent_dir[PATH_MAX];
        safe_copy(parent_dir, sizeof(parent_dir), exe_dir);
        dirname_inplace(parent_dir);
        if (dir_has_spiffs_data(parent_dir)) {
            safe_copy(out, out_size, parent_dir);
            return;
        }
    }

    safe_copy(out, out_size, ".agent-data");
}

static void build_paths(void)
{
    detect_home_dir(s_paths.home, sizeof(s_paths.home));

    join_path2(s_paths.spiffs_base, sizeof(s_paths.spiffs_base), s_paths.home, "spiffs_data");
    join_path2(s_paths.config_dir, sizeof(s_paths.config_dir), s_paths.spiffs_base, "config");
    join_path2(s_paths.memory_dir, sizeof(s_paths.memory_dir), s_paths.spiffs_base, "memory");
    join_path2(s_paths.session_dir, sizeof(s_paths.session_dir), s_paths.spiffs_base, "sessions");
    join_path2(s_paths.cache_dir, sizeof(s_paths.cache_dir), s_paths.spiffs_base, "cache");
    join_path2(s_paths.checkpoint_dir, sizeof(s_paths.checkpoint_dir), s_paths.cache_dir, "checkpoints");
    join_path2(s_paths.web_dir, sizeof(s_paths.web_dir), s_paths.spiffs_base, "web");
    join_path2(s_paths.feishu_image_dir, sizeof(s_paths.feishu_image_dir), s_paths.cache_dir, "feishu_images");
    join_path2(s_paths.skills_dir, sizeof(s_paths.skills_dir), s_paths.spiffs_base, "skills");
    join_path2(s_paths.workspace_dir, sizeof(s_paths.workspace_dir), s_paths.spiffs_base, "workspace");

    join_path2(s_paths.runtime_config_file, sizeof(s_paths.runtime_config_file), s_paths.config_dir, "config.json");
    join_path2(s_paths.bootstrap_file, sizeof(s_paths.bootstrap_file), s_paths.config_dir, "BOOTSTRAP.md");
    join_path2(s_paths.identity_file, sizeof(s_paths.identity_file), s_paths.config_dir, "IDENTITY.md");
    join_path2(s_paths.soul_file, sizeof(s_paths.soul_file), s_paths.config_dir, "SOUL.md");
    join_path2(s_paths.user_file, sizeof(s_paths.user_file), s_paths.config_dir, "USER.md");
    join_path2(s_paths.memory_file, sizeof(s_paths.memory_file), s_paths.memory_dir, "MEMORY.md");
    join_path2(s_paths.skill_review_queue_file, sizeof(s_paths.skill_review_queue_file), s_paths.memory_dir, "SKILL_REVIEW_QUEUE.md");
    join_path2(s_paths.todo_file, sizeof(s_paths.todo_file), s_paths.memory_dir, "TODO.json");
    join_path2(s_paths.work_items_file, sizeof(s_paths.work_items_file), s_paths.memory_dir, "work_items.jsonl");
    join_path2(s_paths.log_file, sizeof(s_paths.log_file), s_paths.memory_dir, "agent.log");
    join_path2(s_paths.web_index_file, sizeof(s_paths.web_index_file), s_paths.web_dir, "index.html");
    join_path2(s_paths.web_css_file, sizeof(s_paths.web_css_file), s_paths.web_dir, "app.css");
    join_path2(s_paths.web_js_file, sizeof(s_paths.web_js_file), s_paths.web_dir, "app.js");
    join_path2(s_paths.prompt_debug_file, sizeof(s_paths.prompt_debug_file), s_paths.cache_dir, "last_prompt.md");
    join_path2(s_paths.cron_file, sizeof(s_paths.cron_file), s_paths.spiffs_base, "cron.json");
    join_path2(s_paths.heartbeat_file, sizeof(s_paths.heartbeat_file), s_paths.spiffs_base, "HEARTBEAT.md");

    char ca_dir[PATH_MAX];
    join_path2(ca_dir, sizeof(ca_dir), s_paths.spiffs_base, "ca");
    join_path2(s_paths.ca_cert_file, sizeof(s_paths.ca_cert_file), ca_dir, "cacert.pem");

    s_paths.initialized = 1;
}

static void ensure_initialized(void)
{
    if (!s_paths.initialized) {
        build_paths();
    }
}

void paths_init(void)
{
    ensure_initialized();
}

#define PATH_GETTER(fn_name, field_name) \
    const char *fn_name(void)            \
    {                                    \
        ensure_initialized();            \
        return s_paths.field_name;       \
    }

PATH_GETTER(path_home, home)
PATH_GETTER(path_spiffs_base, spiffs_base)
PATH_GETTER(path_config_dir, config_dir)
PATH_GETTER(path_memory_dir, memory_dir)
PATH_GETTER(path_session_dir, session_dir)
PATH_GETTER(path_cache_dir, cache_dir)
PATH_GETTER(path_checkpoint_dir, checkpoint_dir)
PATH_GETTER(path_web_dir, web_dir)
PATH_GETTER(path_feishu_image_dir, feishu_image_dir)
PATH_GETTER(path_skills_dir, skills_dir)
PATH_GETTER(path_workspace_dir, workspace_dir)
PATH_GETTER(path_runtime_config_file, runtime_config_file)
PATH_GETTER(path_bootstrap_file, bootstrap_file)
PATH_GETTER(path_identity_file, identity_file)
PATH_GETTER(path_soul_file, soul_file)
PATH_GETTER(path_user_file, user_file)
PATH_GETTER(path_memory_file, memory_file)
PATH_GETTER(path_skill_review_queue_file, skill_review_queue_file)
PATH_GETTER(path_todo_file, todo_file)
PATH_GETTER(path_work_items_file, work_items_file)
PATH_GETTER(path_log_file, log_file)
PATH_GETTER(path_web_index_file, web_index_file)
PATH_GETTER(path_web_css_file, web_css_file)
PATH_GETTER(path_web_js_file, web_js_file)
PATH_GETTER(path_prompt_debug_file, prompt_debug_file)
PATH_GETTER(path_cron_file, cron_file)
PATH_GETTER(path_heartbeat_file, heartbeat_file)
PATH_GETTER(path_ca_cert_file, ca_cert_file)

bool path_is_in_spiffs(const char *path)
{
    ensure_initialized();
    if (!path || !path[0]) return false;

    size_t base_len = strlen(s_paths.spiffs_base);
    if (strncmp(path, s_paths.spiffs_base, base_len) != 0) {
        return false;
    }
    return path[base_len] == '\0' || path[base_len] == '/';
}

bool path_resolve_spiffs_shortcut(const char *path, char *resolved, size_t resolved_size)
{
    ensure_initialized();
    if (!path || !resolved || resolved_size == 0) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }

    if (strcmp(path, "spiffs_data") == 0) {
        return strscpy(resolved, s_paths.spiffs_base, resolved_size) < resolved_size;
    }
    if (strncmp(path, "spiffs_data/", 12) == 0) {
        return snprintf(resolved, resolved_size, "%s/%s", s_paths.spiffs_base, path + 12) < resolved_size;
    }
    return false;
}
