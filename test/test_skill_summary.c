#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "drivers/skill/skill_loader.h"

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
    char skills_dir[512];
    char skill_dir[640];
    char skill_file[768];
    char summary[32768];

    snprintf(home, sizeof(home), "/tmp/agent-skill-summary-home-%ld", (long)getpid());
    snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
    mkdir(home, 0700);
    mkdir(spiffs, 0700);
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir(path_skills_dir(), 0700);
    snprintf(skills_dir, sizeof(skills_dir), "%s", path_skills_dir());

    for (int i = 0; i < 18; i++) {
        snprintf(skill_dir, sizeof(skill_dir), "%s/filler-%02d", skills_dir, i);
        mkdir(skill_dir, 0700);
        snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", skill_dir);
        write_text(skill_file,
                   "---\n"
                   "name: filler\n"
                   "description: This is a deliberately long skill description used to fill the skill summary buffer with enough text that later skills would be truncated if the buffer is too small.\n"
                   "---\n"
                   "\n# Filler\n");
    }

    snprintf(skill_dir, sizeof(skill_dir), "%s/menu-xianren", skills_dir);
    mkdir(skill_dir, 0700);
    snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", skill_dir);
    write_text(skill_file,
               "---\n"
               "name: 菜单仙人\n"
               "description: 当用户询问吃啥或发送食堂菜单时使用。\n"
               "---\n"
               "\n# 菜单仙人\n");

    size_t len = skill_loader_build_summary_for_channel("websocket", summary, sizeof(summary));
    assert(len > 0);
    assert(strstr(summary, "菜单仙人"));
    assert(strstr(summary, "menu-xianren"));

    printf("skill summary tests passed\n");
    return 0;
}
