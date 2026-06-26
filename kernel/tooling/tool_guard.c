/* 工具安全守卫：检测 LLM 幻想的占位符工具名和空参调用，决定是否应中止当前 turn。
 * 识别模式：$TOOL_NAME、<tool_name>、以 $ 开头的未知工具、空 JSON 输入、含 "..." 占位符。 */

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

/** 判断工具调用是否属于不可恢复的协议失败，应中止当前 turn。
 *  @param tool_name   调用的工具名
 *  @param tool_input  工具参数 JSON
 *  @param tool_output 工具输出（未使用）
 *  @param tool_err    工具执行错误码
 *  @return            true=应中止 */
bool agent_tool_protocol_failure_should_stop(const char *tool_name,
                                             const char *tool_input,
                                             const char *tool_output,
                                             err_t tool_err)
{
    (void)tool_output;

    if (tool_err == 0) {
        return false;
    }

    if (is_placeholder_tool_name(tool_name)) {
        return true;
    }

    if (tool_err == ERR_NOT_FOUND && tool_name && tool_name[0] == '$') {
        return true;
    }

    if (tool_input && strcmp(tool_input, "{}") == 0) {
        if (tool_name && strcmp(tool_name, "delegate_task") == 0) {
            return false;
        }
        return true;
    }

    if (text_has_placeholder_value(tool_input)) {
        return true;
    }

    return false;
}
