#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "drivers/skill/skill_tools.h"
#include "drivers/tool/tool_registry.h"

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static int contains_tool_name(const char *json, const char *name)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"name\":\"%s\"", name);
    return json && strstr(json, needle) != NULL;
}

int main(void)
{
    char base_dir[128];
    char skill_dir[192];
    char tools_json_path[256];
    char out[256];

    snprintf(base_dir, sizeof(base_dir), "/tmp/agent-skill-tools-%ld", (long)getpid());
    snprintf(skill_dir, sizeof(skill_dir), "%s/pptx", base_dir);
    assert(mkdir(base_dir, 0700) == 0);
    assert(mkdir(skill_dir, 0700) == 0);
    snprintf(tools_json_path, sizeof(tools_json_path), "%s/TOOLS.json", skill_dir);
    write_text(tools_json_path,
               "["
               "{"
               "\"name\":\"python_exec\","
               "\"description\":\"Execute Python code to generate files.\","
               "\"input_schema_json\":\"{\\\"type\\\":\\\"object\\\",\\\"properties\\\":{\\\"code\\\":{\\\"type\\\":\\\"string\\\"}},\\\"required\\\":[\\\"code\\\"]}\""
               "}"
               "]");

    assert(tool_registry_init() == 0);
    assert(!contains_tool_name(tool_registry_get_tools_json_for_channel("websocket"), "python_exec"));

    assert(skill_tools_register("pptx", skill_dir) == 0);
    assert(contains_tool_name(tool_registry_get_tools_json_for_channel("websocket"), "python_exec"));
    assert(tool_registry_execute("python_exec", "{\"code\":\"print(1)\"}", out, sizeof(out)) == 0);
    assert(strstr(out, "skill tool not yet implemented") != NULL);

    assert(skill_tools_unregister("pptx") == 0);
    assert(!contains_tool_name(tool_registry_get_tools_json_for_channel("websocket"), "python_exec"));

    assert(skill_tools_register("missing-tools", base_dir) == 0);
    assert(!contains_tool_name(tool_registry_get_tools_json_for_channel("websocket"), "python_exec"));

    printf("skill scoped tools tests passed\n");
    return 0;
}
