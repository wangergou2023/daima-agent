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

bool workspace_probe_collect(workspace_probe_result_t *result);
