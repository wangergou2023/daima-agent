/* 工作区探测接口。 */

#pragma once

#include "autoconf.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	bool has_cwd;
	char cwd[BUF_LARGE];
	char agent_workspace[BUF_LARGE];

	bool has_repo_root;
	char repo_root[BUF_LARGE];

	bool has_git;
	bool has_branch;
	char branch[128];
	bool has_status;
	bool is_dirty;
	bool has_commit;
	char commit[256];

	char stack[256];
} workspace_probe_result_t;

typedef struct {
	bool repo_present_before;
	bool repo_ready_after;
	char repo_path[BUF_LARGE];
} workspace_probe_repo_prepare_t;

bool workspace_probe_collect(workspace_probe_result_t *result);
bool workspace_probe_repo_ready(const char *repo_name, char *repo_path, size_t repo_path_size);
bool workspace_probe_ensure_repo_clone(const char *repo_name, const char *clone_url,
				       char *repo_path, size_t repo_path_size);
bool workspace_probe_opencode_repo_ready(char *repo_path, size_t repo_path_size);
bool workspace_probe_ensure_opencode_repo(char *repo_path, size_t repo_path_size);
bool workspace_probe_prepare_opencode_repo(workspace_probe_repo_prepare_t *result);
