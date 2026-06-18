/* 路径解析接口：HOME 检测、SPIFFS 目录布局 getter、快捷路径解析。 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** 显式触发路径初始化（懒初始化，首次调用自动触发）。 */
void paths_init(void);

/* ── 目录路径 getter ── */

const char *path_home(void);              /* Agent 主目录 */
const char *path_spiffs_base(void);       /* SPIFFS 根目录 */
const char *path_config_dir(void);        /* 配置目录 */
const char *path_memory_dir(void);        /* 内存/持久化目录 */
const char *path_session_dir(void);       /* 会话目录 */
const char *path_cache_dir(void);         /* 缓存目录 */
const char *path_checkpoint_dir(void);    /* 检查点目录 */
const char *path_web_dir(void);           /* Web UI 目录 */
const char *path_feishu_image_dir(void);  /* 飞书图片缓存目录 */
const char *path_skills_dir(void);        /* 技能目录 */
const char *path_workspace_dir(void);     /* 工作区目录 */

/* ── 文件路径 getter ── */

const char *path_runtime_config_file(void);       /* config.json */
const char *path_bootstrap_file(void);            /* BOOTSTRAP.md */
const char *path_identity_file(void);             /* IDENTITY.md */
const char *path_soul_file(void);                 /* SOUL.md */
const char *path_user_file(void);                 /* USER.md */
const char *path_memory_file(void);               /* MEMORY.md */
const char *path_skill_review_queue_file(void);   /* SKILL_REVIEW_QUEUE.md */
const char *path_todo_file(void);                 /* TODO.json */
const char *path_work_items_file(void);           /* work_items.jsonl */
const char *path_log_file(void);                  /* agent.log */
const char *path_web_index_file(void);            /* index.html */
const char *path_web_css_file(void);              /* app.css */
const char *path_web_js_file(void);               /* app.js */
const char *path_prompt_debug_file(void);         /* last_prompt.md */
const char *path_cron_file(void);                 /* cron.json */
const char *path_heartbeat_file(void);            /* HEARTBEAT.md */
const char *path_ca_cert_file(void);              /* cacert.pem */

/** 判断路径是否在 SPIFFS 目录下。 */
bool path_is_in_spiffs(const char *path);

/** 解析 "spiffs_data" 快捷路径为绝对路径（拒绝 ".." 路径穿越）。 */
bool path_resolve_spiffs_shortcut(const char *path, char *resolved, size_t resolved_size);
