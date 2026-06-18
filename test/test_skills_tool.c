#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "drivers/tool/tool_skills.h"

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
    char workdir[256];
    char skill_dir[512];
    char skill_file[640];
    char out[8192];

    snprintf(home, sizeof(home), "/tmp/agent-skills-tool-home-%ld", (long)getpid());
    snprintf(workdir, sizeof(workdir), "%s/spiffs_data/workspace", home);
    mkdir(home, 0700);
    char spiffs[256];
    snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
    mkdir(spiffs, 0700);
    mkdir(workdir, 0700);
    setenv("AGENT_HOME", home, 1);
    paths_init();
    mkdir(path_skills_dir(), 0700);

    snprintf(skill_dir, sizeof(skill_dir), "%s/demo-skill", path_skills_dir());
    mkdir(skill_dir, 0700);
    snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", skill_dir);
    write_text(skill_file, "---\nname: demo-skill\ndescription: Demo skill for tests\n---\n\n# Demo\n");

    assert(tool_skills_execute("{\"action\":\"list\",\"pattern\":\"demo\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "demo-skill"));

    assert(tool_skills_execute("{\"action\":\"view\",\"name\":\"demo-skill\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "# Demo"));

    assert(tool_skills_execute("{\"pattern\":\"demo\"}", out, sizeof(out)) == ERR_INVALID_ARG);
    assert(strstr(out, "action"));

    printf("skills tool tests passed\n");
    return 0;
}
