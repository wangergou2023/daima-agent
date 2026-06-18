#include "paths.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

int main(void)
{
    char home[512];
    char cwd[512];
    char expected[1024];
    snprintf(home, sizeof(home), "/tmp/agent-paths-home-%ld", (long)getpid());
    snprintf(cwd, sizeof(cwd), "/tmp/agent-paths-cwd-%ld", (long)getpid());
    mkdir_p(home);
    mkdir_p(cwd);
    unsetenv("AGENT_HOME");
    setenv("HOME", home, 1);
    assert(chdir(cwd) == 0);

    paths_init();
    snprintf(expected, sizeof(expected), "%s/.agent-data", home);
    assert(strcmp(path_home(), expected) == 0);

    printf("paths tests passed\n");
    return 0;
}
