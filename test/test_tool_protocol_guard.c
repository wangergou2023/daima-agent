#include "tool_guard.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void)
{
    assert(agent_tool_protocol_failure_should_stop(
        "apply_patch",
        "{}",
        "错误：缺少 'patch' 字段",
        ERR_INVALID_ARG));

    assert(agent_tool_protocol_failure_should_stop(
        "$TOOL_NAME",
        "{}",
        "错误：未知工具 '$TOOL_NAME'",
        -ENODEV));

    assert(!agent_tool_protocol_failure_should_stop(
        "webfetch",
        "{\"url\":\"https://example.com\"}",
        "HTTP 503",
        -EIO));

    assert(!agent_tool_protocol_failure_should_stop(
        "terminal",
        "{\"command\":\"make test\"}",
        "{\"exit_code\":2}",
        0));

    printf("tool protocol guard tests passed\n");
    return 0;
}
