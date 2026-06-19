#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "context_build.h"
#include "paths.h"
#include "autoconf.h"

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
    char dir[512];
    char file[640];
    char prompt[CONTEXT_BUF_SIZE];

    snprintf(home, sizeof(home), "/tmp/agent-context-skills-home-%ld", (long)getpid());
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

    char soul_path[512], user_path[512];
    snprintf(soul_path, sizeof(soul_path), "%s/SOUL.md", path_config_dir());
    snprintf(user_path, sizeof(user_path), "%s/USER.md", path_config_dir());
    write_text(soul_path, "# SOUL\n");
    write_text(user_path, "# USER\n");
    char mem_path[512];
    snprintf(mem_path, sizeof(mem_path), "%s/MEMORY.md", path_memory_dir());
    write_text(mem_path, "# MEMORY\n");

    for (int i = 0; i < 18; i++) {
        snprintf(dir, sizeof(dir), "%s/filler-%02d", skills_dir, i);
        mkdir(dir, 0700);
        snprintf(file, sizeof(file), "%s/SKILL.md", dir);
        write_text(file,
                   "---\n"
                   "name: filler\n"
                   "description: This is a deliberately long skill description used to fill the skill summary buffer with enough text that later skills would be truncated if the buffer is too small.\n"
                   "---\n"
                   "\n# Filler\n");
    }

    snprintf(dir, sizeof(dir), "%s/menu-xianren", skills_dir);
    mkdir(dir, 0700);
    snprintf(file, sizeof(file), "%s/SKILL.md", dir);
    write_text(file,
               "---\n"
               "name: 菜单仙人\n"
               "description: 当用户询问吃啥或发送食堂菜单时使用。\n"
               "---\n"
               "\n# 菜单仙人\n");

    assert(context_build_system_prompt_for_channel("websocket", prompt, sizeof(prompt)) == 0);
    assert(strstr(prompt, "menu-xianren"));
    assert(strstr(prompt, "菜单仙人"));

    printf("context skills summary tests passed\n");
    return 0;
}
