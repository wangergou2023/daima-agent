#pragma once

#include <stdbool.h>
#include <stddef.h>

void paths_init(void);

const char *path_home(void);
const char *path_spiffs_base(void);
const char *path_config_dir(void);
const char *path_memory_dir(void);
const char *path_session_dir(void);
const char *path_cache_dir(void);
const char *path_checkpoint_dir(void);
const char *path_web_dir(void);
const char *path_feishu_image_dir(void);
const char *path_skills_dir(void);
const char *path_workspace_dir(void);

const char *path_runtime_config_file(void);
const char *path_bootstrap_file(void);
const char *path_identity_file(void);
const char *path_soul_file(void);
const char *path_user_file(void);
const char *path_memory_file(void);
const char *path_skill_review_queue_file(void);
const char *path_todo_file(void);
const char *path_work_items_file(void);
const char *path_log_file(void);
const char *path_web_index_file(void);
const char *path_web_css_file(void);
const char *path_web_js_file(void);
const char *path_prompt_debug_file(void);
const char *path_cron_file(void);
const char *path_heartbeat_file(void);
const char *path_ca_cert_file(void);

bool path_is_in_spiffs(const char *path);
bool path_resolve_spiffs_shortcut(const char *path, char *resolved, size_t resolved_size);
