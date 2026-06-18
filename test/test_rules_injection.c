#include "rules.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/agent-rules-test-%ld", (long)getpid());
    assert(mkdir(tmpdir, 0777) == 0);
    assert(chdir(tmpdir) == 0);
    assert(mkdir("spiffs_data", 0777) == 0);
    assert(mkdir("spiffs_data/config", 0777) == 0);

    write_file("AGENTS.md", "root-rule\n");
    write_file("spiffs_data/config/AGENTS.md", "config-rule\n");

    char buf[8192];
    assert(rules_injection_load(buf, sizeof(buf)) == 0);
    assert(strstr(buf, "## 项目规则\n\nroot-rule\n") == buf);
    assert(strstr(buf, "config-rule\n") != NULL);
    assert(strstr(buf, "root-rule\n") < strstr(buf, "config-rule\n"));

    char small[32];
    assert(rules_injection_load(small, sizeof(small)) == 0);
    assert(small[sizeof(small) - 1] == '\0');
    assert(strstr(small, "## 项目规则\n\n") == small);

    printf("rules injection tests passed\n");
    return 0;
}
