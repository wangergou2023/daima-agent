#pragma once

#include <stdbool.h>
#include <stddef.h>

void daima_paths_init(void);

const char *daima_path_home(void);
const char *daima_path_spiffs_base(void);
const char *daima_path_config_dir(void);
const char *daima_path_memory_dir(void);
const char *daima_path_session_dir(void);
const char *daima_path_cache_dir(void);
const char *daima_path_checkpoint_dir(void);
const char *daima_path_web_dir(void);
const char *daima_path_feishu_image_dir(void);
const char *daima_path_skills_dir(void);

const char *daima_path_runtime_config_file(void);
const char *daima_path_bootstrap_file(void);
const char *daima_path_identity_file(void);
const char *daima_path_soul_file(void);
const char *daima_path_user_file(void);
const char *daima_path_memory_file(void);
const char *daima_path_skill_review_queue_file(void);
const char *daima_path_todo_file(void);
const char *daima_path_work_items_file(void);
const char *daima_path_log_file(void);
const char *daima_path_web_index_file(void);
const char *daima_path_web_css_file(void);
const char *daima_path_web_js_file(void);
const char *daima_path_prompt_debug_file(void);
const char *daima_path_cron_file(void);
const char *daima_path_heartbeat_file(void);
const char *daima_path_ca_cert_file(void);

bool daima_path_is_in_spiffs(const char *path);
bool daima_path_resolve_spiffs_shortcut(const char *path, char *resolved, size_t resolved_size);
