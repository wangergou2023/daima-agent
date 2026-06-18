#include "paths.h"
#include "drivers/tool/tool_hashline.h"
#include "drivers/tool/tool_files.h"

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

static void read_file_text(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    assert(f);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void write_file_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    fclose(f);
}

int main(void)
{
    char home[512];
    char workdir[512];
    snprintf(home, sizeof(home), "/tmp/agent-apply-patch-home-%ld", (long)getpid());
    snprintf(workdir, sizeof(workdir), "/tmp/agent-apply-patch-work-%ld", (long)getpid());
    mkdir_p(home);
    mkdir_p(workdir);
    setenv("AGENT_HOME", home, 1);
    assert(chdir(workdir) == 0);
    paths_init();
    mkdir_p(path_workspace_dir());

    char out[4096];
    char text[256];
    char target[1024];

    int err = tool_apply_patch_execute(
        "{\"patch\":\"*** Begin Patch\\n*** Add File: notes/one.txt\\n+alpha\\n+beta\\n*** End Patch\\n\"}",
        out,
        sizeof(out));
    if (err) {
        fprintf(stderr, "add failed: err=%d out=%s\n", err, out);
    }
    assert(!err);
    snprintf(target, sizeof(target), "%s/notes/one.txt", path_workspace_dir());
    read_file_text(target, text, sizeof(text));
    assert(strcmp(text, "alpha\nbeta\n") == 0);

    err = tool_apply_patch_execute(
        "{\"patch\":\"*** Begin Patch\\n*** Add File: notes/plain.txt\\ngamma\\ndelta\\n*** End Patch\\n\"}",
        out,
        sizeof(out));
    if (err) {
        fprintf(stderr, "plain add failed: err=%d out=%s\n", err, out);
    }
    assert(!err);
    snprintf(target, sizeof(target), "%s/notes/plain.txt", path_workspace_dir());
    read_file_text(target, text, sizeof(text));
    assert(strcmp(text, "gamma\ndelta\n") == 0);

    snprintf(target, sizeof(target), "%s/code.txt", path_workspace_dir());
    write_file_text(target, "one\ntwo\nthree\n");
    err = tool_apply_patch_execute(
        "{\"patch\":\"*** Begin Patch\\n*** Update File: code.txt\\n@@\\n one\\n-two\\n+TWO\\n three\\n*** End Patch\\n\"}",
        out,
        sizeof(out));
    if (err) {
        fprintf(stderr, "update failed: err=%d out=%s\n", err, out);
    }
    assert(!err);
    read_file_text(target, text, sizeof(text));
    assert(strcmp(text, "one\nTWO\nthree\n") == 0);

    char hash[5] = {0};
    hashline_hash_line("TWO", hash);
    char patch[512];
    snprintf(patch, sizeof(patch),
             "{\"patch\":\"*** Begin Patch\\n*** Update File: code.txt\\n@@\\n one\\n-%d#%s|TWO\\n+TWO!\\n three\\n*** End Patch\\n\"}",
             2, hash);
    err = tool_apply_patch_execute(patch, out, sizeof(out));
    if (err) {
        fprintf(stderr, "hashline update failed: err=%d out=%s\n", err, out);
    }
    assert(!err);
    read_file_text(target, text, sizeof(text));
    assert(strcmp(text, "one\nTWO!\nthree\n") == 0);

    snprintf(patch, sizeof(patch),
             "{\"patch\":\"*** Begin Patch\\n*** Update File: code.txt\\n@@\\n one\\n-%d#%s|TWO!\\n+bad\\n three\\n*** End Patch\\n\"}",
             2, hash);
    err = tool_apply_patch_execute(patch, out, sizeof(out));
    assert(err == ERR_INVALID_STATE);
    read_file_text(target, text, sizeof(text));
    assert(strcmp(text, "one\nTWO!\nthree\n") == 0);

    err = tool_apply_patch_execute(
        "{\"patch\":\"*** Begin Patch\\n*** Delete File: code.txt\\n*** End Patch\\n\"}",
        out,
        sizeof(out));
    if (err) {
        fprintf(stderr, "delete failed: err=%d out=%s\n", err, out);
    }
    assert(!err);
    assert(access(target, F_OK) != 0);

    printf("apply_patch tests passed\n");
    return 0;
}
