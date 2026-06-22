#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "autoconf.h"
#include "context_build.h"
#include "paths.h"

static void write_text(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

int main(void)
{
	char home[128];
	char spiffs[256];
	char config_dir[256];
	char memory_dir[256];
	char skills_dir[256];
	char prompt[CONTEXT_BUF_SIZE];
	char repo_root[512];

	assert(realpath("/home/wangergou/code/github/daima-agent", repo_root) != NULL);
	assert(chdir(repo_root) == 0);

	snprintf(home, sizeof(home), "/tmp/agent-context-workspace-home-%ld", (long)getpid());
	snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
	snprintf(config_dir, sizeof(config_dir), "%s/config", spiffs);
	snprintf(memory_dir, sizeof(memory_dir), "%s/memory", spiffs);
	snprintf(skills_dir, sizeof(skills_dir), "%s/skills", spiffs);

	mkdir(home, 0700);
	mkdir(spiffs, 0700);
	mkdir(config_dir, 0700);
	mkdir(memory_dir, 0700);
	mkdir(skills_dir, 0700);

	setenv("AGENT_HOME", home, 1);
	paths_init();

	char soul_path[512], user_path[512], mem_path[512];
	snprintf(soul_path, sizeof(soul_path), "%s/SOUL.md", path_config_dir());
	snprintf(user_path, sizeof(user_path), "%s/USER.md", path_config_dir());
	snprintf(mem_path, sizeof(mem_path), "%s/MEMORY.md", path_memory_dir());
	write_text(soul_path, "# SOUL\n");
	write_text(user_path, "# USER\n");
	write_text(mem_path, "# MEMORY\n");

	assert(context_build_system_prompt_for_channel("websocket", prompt, sizeof(prompt)) == 0);
	assert(strstr(prompt, "## 当前工作区"));
	assert(strstr(prompt, "- cwd: `"));
	assert(strstr(prompt, "- agent workspace: `"));
	assert(strstr(prompt, path_workspace_dir()));
	assert(strstr(prompt, "repo root: `/home/wangergou/code/github/daima-agent`"));
	assert(strstr(prompt, "branch `main`"));
	assert(strstr(prompt, "latest `6d6819d 删除没用的文件`"));

	printf("context workspace tests passed\n");
	return 0;
}
