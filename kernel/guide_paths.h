/* 引导文件路径与文案辅助。 */

#pragma once

#include <stddef.h>

typedef struct {
	char bootstrap_path[512];
	char identity_path[512];
	char soul_path[512];
	char user_path[512];
} guide_paths_t;

void guide_paths_init(guide_paths_t *paths);

size_t guide_paths_append_runtime_guide(char *buf, size_t size, size_t offset,
					 const guide_paths_t *paths);
