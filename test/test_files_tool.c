#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "drivers/tool/tool_files.h"

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
    char file[512];
    char out[8192];

    snprintf(home, sizeof(home), "/tmp/agent-files-tool-home-%ld", (long)getpid());
    snprintf(workdir, sizeof(workdir), "%s/spiffs_data/workspace", home);
    mkdir(home, 0700);
    char spiffs[256];
    snprintf(spiffs, sizeof(spiffs), "%s/spiffs_data", home);
    mkdir(spiffs, 0700);
    mkdir(workdir, 0700);
    setenv("AGENT_HOME", home, 1);
    assert(chdir(workdir) == 0);
    paths_init();

    snprintf(file, sizeof(file), "%s/alpha.txt", workdir);
    write_text(file, "one\nneedle\nthree\n");

    assert(tool_files_execute("{\"action\":\"read\",\"path\":\"alpha.txt\",\"limit\":2}", out, sizeof(out)) == 0);
    assert(strstr(out, "one"));
    assert(strstr(out, "needle"));

    assert(tool_files_execute("{\"action\":\"list\",\"path\":\".\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "alpha.txt"));

    assert(tool_files_execute("{\"action\":\"search\",\"pattern\":\"needle\",\"path\":\".\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "needle"));

    assert(tool_files_execute("{\"path\":\"alpha.txt\"}", out, sizeof(out)) == ERR_INVALID_ARG);
    assert(strstr(out, "action"));

    printf("files tool tests passed\n");
    return 0;
}
