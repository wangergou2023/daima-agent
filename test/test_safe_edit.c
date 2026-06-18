#include "drivers/tool/tool_safe_edit.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int printk(const char *fmt, ...)
{
    (void)fmt;

    return 0;
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    fclose(f);
}

int main(void)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/agent-safe-edit-%ld.txt", (long)getpid());

    write_text(path, "one\ntwo\nthree\n");
    safe_edit_clear_all();
    assert(safe_edit_register_read(path, "one\ntwo\n", 1, 2) == 0);

    write_text(path, "one\nTWO\nthree\n");
    assert(safe_edit_verify(path, "unused patch content") == ERR_INVALID_STATE);

    unlink(path);
    safe_edit_clear_all();
    printf("safe_edit tests passed\n");
    return 0;
}
