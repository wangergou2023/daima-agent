#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "drivers/tool/tool_registry.h"

static int contains_tool_name(const char *json, const char *name)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"name\":\"%s\"", name);
    return json && strstr(json, needle) != NULL;
}

int main(void)
{
    char out[512];

    assert(tool_registry_init() == 0);

    const char *json = tool_registry_get_tools_json_for_channel("websocket");
    assert(json);

    assert(contains_tool_name(json, "files"));
    assert(contains_tool_name(json, "apply_patch"));
    assert(contains_tool_name(json, "restore_file"));
    assert(contains_tool_name(json, "cron"));
    assert(contains_tool_name(json, "skills"));

    assert(!contains_tool_name(json, "read_file"));
    assert(!contains_tool_name(json, "list_dir"));
    assert(!contains_tool_name(json, "search_files"));
    assert(!contains_tool_name(json, "edit_file"));
    assert(!contains_tool_name(json, "patch"));
    assert(!contains_tool_name(json, "cron_add"));
    assert(!contains_tool_name(json, "cron_list"));
    assert(!contains_tool_name(json, "cron_remove"));
    assert(!contains_tool_name(json, "skills_list"));
    assert(!contains_tool_name(json, "skill_view"));

    assert(tool_registry_execute("read_file", "{\"path\":\"x\"}", out, sizeof(out)) == ERR_NOT_FOUND);
    assert(tool_registry_execute("cron_add", "{}", out, sizeof(out)) == ERR_NOT_FOUND);
    assert(tool_registry_execute("skill_view", "{}", out, sizeof(out)) == ERR_NOT_FOUND);

    printf("tool surface tests passed\n");
    return 0;
}
