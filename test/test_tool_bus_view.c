#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "drivers/tool/tool_builtin_bus.h"
#include "drivers/tool/tool_custom.h"
#include "drivers/tool/tool_bus_view.h"
#include "drivers/tool/tool_types.h"
#include "linux/bus.h"

static int contains_tool_name(const char *json, const char *name)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"name\":\"%s\"", name);
    return json && strstr(json, needle) != NULL;
}

int main(void)
{
    assert(bus_init() == 0);
    assert(tool_builtin_bus_init() == 0);
    assert(tool_custom_load_default() >= 0);

    const struct tool_device *files = tool_bus_get_device("files");
    assert(files != NULL);
    assert(files->description != NULL);
    assert(files->input_schema_json != NULL);

    const char *json = tool_bus_tools_json_for_channel("websocket");
    assert(json != NULL);
    assert(contains_tool_name(json, "files"));
    assert(contains_tool_name(json, "apply_patch"));
    assert(!contains_tool_name(json, "robot_drive_straight"));

    json = tool_bus_tools_json_for_channel("vector");
    assert(json != NULL);
    assert(contains_tool_name(json, "robot_drive_straight"));

    char output[256];
    memset(output, 0, sizeof(output));
    assert(tool_bus_execute("get_current_time", "{}", output, sizeof(output)) == 0);
    assert(output[0] != '\0');

    printf("tool bus view tests passed\n");
    return 0;
}
