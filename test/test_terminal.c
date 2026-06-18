#include "paths.h"
#include "runtime.h"
#include "drivers/tool/tool_system.h"

#include "cjson.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

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

static void trim_newline(char *s)
{
    size_t len = s ? strlen(s) : 0;
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static void write_file_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

int main(void)
{
    char home[256];
    char process_cwd[256];
    snprintf(home, sizeof(home), "/tmp/agent-terminal-home-%ld", (long)getpid());
    snprintf(process_cwd, sizeof(process_cwd), "/tmp/agent-terminal-cwd-%ld", (long)getpid());
    mkdir_p(home);
    mkdir_p(process_cwd);
    setenv("AGENT_HOME", home, 1);
    assert(chdir(process_cwd) == 0);
    paths_init();
    mkdir_p(path_workspace_dir());

    char out[4096];
    assert(tool_terminal_execute("{\"command\":\"pwd\",\"timeout\":5}", out, sizeof(out)) == 0);

    cJSON *root = cJSON_Parse(out);
    assert(root);
    const char *workdir = cJSON_GetStringValue(cJSON_GetObjectItem(root, "workdir"));
    const char *output = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output"));
    assert(workdir);
    assert(output);
    assert(strcmp(workdir, path_workspace_dir()) == 0);

    char pwd[1024];
    snprintf(pwd, sizeof(pwd), "%s", output);
    trim_newline(pwd);
    assert(strcmp(pwd, path_workspace_dir()) == 0);
    assert(strcmp(pwd, process_cwd) != 0);

    cJSON_Delete(root);

    assert(tool_terminal_execute("{\"command\":\"node -e \\\"console.log(1)\\\"\",\"timeout\":5}", out, sizeof(out)) == 0);
    root = cJSON_Parse(out);
    assert(root);
    output = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output"));
    assert(output && strstr(output, "1"));
    cJSON_Delete(root);

    char config_dir[512];
    char config_file[1024];
    snprintf(config_dir, sizeof(config_dir), "%s/spiffs_data/config", home);
    mkdir_p(config_dir);
    snprintf(config_file, sizeof(config_file), "%s/config.json", config_dir);
    write_file_text(config_file, "{\"common\":{\"terminal_security_level\":\"plan\"}}\n");
    assert(runtime_config_init() == 0);

    assert(tool_terminal_execute("{\"command\":\"node -e \\\"console.log(2)\\\"\",\"timeout\":5}", out, sizeof(out)) == ERR_INVALID_STATE);
    root = cJSON_Parse(out);
    assert(root);
    const char *error = cJSON_GetStringValue(cJSON_GetObjectItem(root, "error"));
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(root, "message"));
    assert(error && strcmp(error, "inline_code_blocked") == 0);
    assert(message && strstr(message, "apply_patch"));
    assert(message && strstr(message, "node script.js"));
    cJSON_Delete(root);

    write_file_text(config_file, "{\"common\":{\"terminal_security_level\":\"build\"}}\n");
    assert(runtime_config_init() == 0);
    assert(tool_terminal_execute("{\"command\":\"node -e \\\"console.log(123)\\\"\",\"timeout\":5}", out, sizeof(out)) == 0);
    root = cJSON_Parse(out);
    assert(root);
    output = cJSON_GetStringValue(cJSON_GetObjectItem(root, "output"));
    assert(output && strstr(output, "123"));
    cJSON_Delete(root);

    printf("terminal tests passed\n");
    return 0;
}
