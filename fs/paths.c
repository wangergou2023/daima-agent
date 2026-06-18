/* 路径解析：HOME 检测、路径拼接、SPIFFS 目录布局、快捷路径解析。 */

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

/*
 * 路径层次结构：
 *   agent_home/                         ← AGENT_HOME 或 ~/.agent-data 或 exe 目录
 *   └── spiffs_data/                    ← 运行时数据根目录
 *       ├── config/                     ← 配置文件（config.json, IDENTITY.md, SOUL.md 等）
 *       ├── memory/                     ← 持久化内存（MEMORY.md, TODO.json, agent.log）
 *       ├── sessions/                   ← 会话存储
 *       ├── cache/                     ← 缓存（checkpoints/, feishu_images/, last_prompt.md）
 *       ├── web/                       ← Web UI 静态文件
 *       ├── skills/                    ← 技能目录
 *       ├── workspace/                 ← 工作区
 *       ├── ca/cacert.pem              ← CA 证书
 *       ├── cron.json                  ← 定时任务持久化
 *       └── HEARTBEAT.md               ← 心跳文件
 */
typedef struct {
    int initialized;                     /* 是否已初始化 */
    char home[PATH_MAX];                 /* Agent 主目录 */
    char spiffs_base[PATH_MAX];          /* SPIFFS 根目录 */
    char config_dir[PATH_MAX];           /* 配置目录 */
    char memory_dir[PATH_MAX];           /* 内存/持久化目录 */
    char session_dir[PATH_MAX];          /* 会话目录 */
    char cache_dir[PATH_MAX];            /* 缓存目录 */
    char checkpoint_dir[PATH_MAX];       /* 检查点目录 */
    char web_dir[PATH_MAX];              /* Web UI 目录 */
    char feishu_image_dir[PATH_MAX];     /* 飞书图片缓存目录 */
    char skills_dir[PATH_MAX];           /* 技能目录 */
    char workspace_dir[PATH_MAX];        /* 工作区目录 */
    char runtime_config_file[PATH_MAX];  /* 运行时配置: config.json */
    char bootstrap_file[PATH_MAX];       /* 启动提示: BOOTSTRAP.md */
    char identity_file[PATH_MAX];        /* 身份定义: IDENTITY.md */
    char soul_file[PATH_MAX];            /* 灵魂约束: SOUL.md */
    char user_file[PATH_MAX];            /* 用户信息: USER.md */
    char memory_file[PATH_MAX];          /* 持久内存: MEMORY.md */
    char skill_review_queue_file[PATH_MAX]; /* 技能评审队列: SKILL_REVIEW_QUEUE.md */
    char todo_file[PATH_MAX];            /* TODO 持久化: TODO.json */
    char work_items_file[PATH_MAX];      /* 工作项日志: work_items.jsonl */
    char log_file[PATH_MAX];             /* 日志文件: agent.log */
    char web_index_file[PATH_MAX];       /* Web 首页: index.html */
    char web_css_file[PATH_MAX];         /* Web 样式: app.css */
    char web_js_file[PATH_MAX];          /* Web 脚本: app.js */
    char prompt_debug_file[PATH_MAX];    /* Prompt 调试: last_prompt.md */
    char cron_file[PATH_MAX];            /* Cron 持久化: cron.json */
    char heartbeat_file[PATH_MAX];       /* 心跳文件: HEARTBEAT.md */
    char ca_cert_file[PATH_MAX];         /* CA 证书: cacert.pem */
} paths_state_t;

static paths_state_t s_paths = {0};

/**
 * 安全复制字符串，使用 strscpy 防止溢出。
 * @param dst      目标缓冲区
 * @param dst_size 缓冲区大小
 * @param src      源字符串
 */
static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    strscpy(dst, src ? src : "", dst_size);
}

/**
 * 路径拼接：若 a 以 '/' 结尾则直接拼接，否则加 '/' 分隔。
 * @param dst      输出缓冲区
 * @param dst_size 缓冲区大小
 * @param a        路径前缀
 * @param b        路径后缀
 */
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

/**
 * 检查目录下是否存在 spiffs_data 子目录。
 * @param dir 待检查的目录路径
 * @return 存在返回 true
 */
static bool dir_has_spiffs_data(const char *dir)
{
    if (!dir || !dir[0]) return false;
    char candidate[PATH_MAX];
    join_path2(candidate, sizeof(candidate), dir, "spiffs_data");
    return access(candidate, F_OK) == 0;
}

/**
 * 通过 /proc/self/exe 获取可执行文件所在目录。
 * @param out      输出缓冲区
 * @param out_size 缓冲区大小
 * @return 成功返回 true
 */
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

/**
 * 原地获取目录名（删除最后一个 '/' 之后的部分）。
 * @param path 路径字符串（原地修改）
 */
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

/**
 * 检测 Agent 主目录。优先级：
 *   1. 环境变量 AGENT_HOME
 *   2. ~/.agent-data（$HOME/.agent-data）
 *   3. 可执行文件所在目录（若含 spiffs_data/）
 *   4. 可执行文件父目录（若含 spiffs_data/）
 *   5. 回退到 ./.agent-data
 * @param out      输出缓冲区
 * @param out_size 缓冲区大小
 */
static void detect_home_dir(char *out, size_t out_size)
{
    /* 1. 环境变量 AGENT_HOME */
    const char *env_home = getenv(AGENT_HOME_ENV);
    if (env_home && env_home[0]) {
        safe_copy(out, out_size, env_home);
        return;
    }

    /* 2. $HOME/.agent-data */
    const char *home_env = getenv("HOME");
    if (home_env && home_env[0]) {
        snprintf(out, out_size, "%s/%s", home_env, DEFAULT_HOME_NAME);
        return;
    }

    /* 3-4. 可执行文件目录（含 spiffs_data 检查） */
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

    /* 5. 回退 */
    safe_copy(out, out_size, ".agent-data");
}

/**
 * 构建所有路径。基于检测到的 agent_home 目录，生成完整的 SPIFFS 目录树。
 * 路径层次见文件头注释。
 */
static void build_paths(void)
{
    detect_home_dir(s_paths.home, sizeof(s_paths.home));

    /* 目录路径 */
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

    /* 配置文件 (config/) */
    join_path2(s_paths.runtime_config_file, sizeof(s_paths.runtime_config_file), s_paths.config_dir, "config.json");
    join_path2(s_paths.bootstrap_file, sizeof(s_paths.bootstrap_file), s_paths.config_dir, "BOOTSTRAP.md");
    join_path2(s_paths.identity_file, sizeof(s_paths.identity_file), s_paths.config_dir, "IDENTITY.md");
    join_path2(s_paths.soul_file, sizeof(s_paths.soul_file), s_paths.config_dir, "SOUL.md");
    join_path2(s_paths.user_file, sizeof(s_paths.user_file), s_paths.config_dir, "USER.md");

    /* 持久化文件 (memory/) */
    join_path2(s_paths.memory_file, sizeof(s_paths.memory_file), s_paths.memory_dir, "MEMORY.md");
    join_path2(s_paths.skill_review_queue_file, sizeof(s_paths.skill_review_queue_file), s_paths.memory_dir, "SKILL_REVIEW_QUEUE.md");
    join_path2(s_paths.todo_file, sizeof(s_paths.todo_file), s_paths.memory_dir, "TODO.json");
    join_path2(s_paths.work_items_file, sizeof(s_paths.work_items_file), s_paths.memory_dir, "work_items.jsonl");
    join_path2(s_paths.log_file, sizeof(s_paths.log_file), s_paths.memory_dir, "agent.log");

    /* Web UI 文件 (web/) */
    join_path2(s_paths.web_index_file, sizeof(s_paths.web_index_file), s_paths.web_dir, "index.html");
    join_path2(s_paths.web_css_file, sizeof(s_paths.web_css_file), s_paths.web_dir, "app.css");
    join_path2(s_paths.web_js_file, sizeof(s_paths.web_js_file), s_paths.web_dir, "app.js");

    /* 缓存/调试文件 */
    join_path2(s_paths.prompt_debug_file, sizeof(s_paths.prompt_debug_file), s_paths.cache_dir, "last_prompt.md");

    /* SPIFFS 根文件 */
    join_path2(s_paths.cron_file, sizeof(s_paths.cron_file), s_paths.spiffs_base, "cron.json");
    join_path2(s_paths.heartbeat_file, sizeof(s_paths.heartbeat_file), s_paths.spiffs_base, "HEARTBEAT.md");

    /* CA 证书 */
    char ca_dir[PATH_MAX];
    join_path2(ca_dir, sizeof(ca_dir), s_paths.spiffs_base, "ca");
    join_path2(s_paths.ca_cert_file, sizeof(s_paths.ca_cert_file), ca_dir, "cacert.pem");

    s_paths.initialized = 1;
}

/** 确保路径已初始化（懒初始化）。 */
static void ensure_initialized(void)
{
    if (!s_paths.initialized) {
        build_paths();
    }
}

/** 显式触发路径初始化。 */
void paths_init(void)
{
    ensure_initialized();
}

/* 路径 getter 宏：每个函数返回对应字段的 const char * */
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

/**
 * 判断路径是否在 SPIFFS 目录下。
 * @param path 待检查的路径
 * @return 在 SPIFFS 下返回 true
 */
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

/**
 * 解析 "spiffs_data" 快捷路径为绝对路径。
 * 例如 "spiffs_data/config" → "/home/user/.agent-data/spiffs_data/config"
 * 拒绝包含 ".." 的路径以防目录遍历攻击。
 * @param path          快捷路径（如 "spiffs_data" 或 "spiffs_data/config"）
 * @param resolved      输出：解析后的绝对路径
 * @param resolved_size 输出缓冲区大小
 * @return 成功返回 true
 */
bool path_resolve_spiffs_shortcut(const char *path, char *resolved, size_t resolved_size)
{
    ensure_initialized();
    if (!path || !resolved || resolved_size == 0) {
        return false;
    }
    /* 安全检查：拒绝路径穿越 */
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
