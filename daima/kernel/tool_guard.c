#include "tool_guard.h"

#include <string.h>

static bool text_has_placeholder_value(const char *text)
{
    return text && strstr(text, "\":\"...\"") != NULL;
}

static bool is_placeholder_tool_name(const char *tool_name)
{
    return tool_name &&
           (strcmp(tool_name, "$TOOL_NAME") == 0 ||
            strcmp(tool_name, "<tool_name>") == 0 ||
            strcmp(tool_name, "tool_name") == 0);
}

bool agent_tool_protocol_failure_should_stop(const char *tool_name,
                                             const char *tool_input,
                                             const char *tool_output,
                                             daima_err_t tool_err)
{
    (void)tool_output;

    if (tool_err == DAIMA_OK) {
        return false;
    }

    if (is_placeholder_tool_name(tool_name)) {
        return true;
    }

    if (tool_err == DAIMA_ERR_NOT_FOUND && tool_name && tool_name[0] == '$') {
        return true;
    }

    if (tool_input && strcmp(tool_input, "{}") == 0) {
        return true;
    }

    if (text_has_placeholder_value(tool_input)) {
        return true;
    }

    return false;
}
