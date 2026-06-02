/* Vector 音频工具: set_volume */
#include "tools/tool_vector_common.h"
#include "tools/tool_registry.h"
#include "channels/vector/vector_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "tool_vector_audio";

static mcp_client_t *get_mcp(char *output, size_t size) {
    mcp_client_t *m = vector_channel_get_mcp();
    if (!m) snprintf(output, size, "错误：Vector 机器人未连接");
    return m;
}

static daima_err_t call_mcp_with_args(mcp_client_t *mcp, const char *tool_name, cJSON *args,
                                      char *output, size_t output_size)
{
    char *args_json = cJSON_PrintUnformatted(args);
    if (!args_json) {
        cJSON_Delete(args);
        snprintf(output, output_size, "错误：JSON 序列化失败");
        return DAIMA_ERR_NO_MEM;
    }
    daima_err_t err = mcp_client_call_tool(mcp, tool_name, args_json, output, output_size);
    free(args_json);
    cJSON_Delete(args);
    return err;
}

/* ---- Set Volume ---- */
static daima_err_t tool_robot_set_volume_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;

    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }
    cJSON *lev = cJSON_GetObjectItem(in, "level");
    int level = lev && cJSON_IsNumber(lev) ? (int)lev->valuedouble : 2;
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    cJSON_Delete(in);

    cJSON *args = cJSON_CreateObject();
    if (!args) return DAIMA_ERR_NO_MEM;
    cJSON_AddNumberToObject(args, "level", level);
    return call_mcp_with_args(mcp, "robot_set_volume", args, output, output_size);
}

static const daima_tool_t s_set_volume = {
    .name = "robot_set_volume",
    .description = "设置 Vector 机器人主音量。level: 0(最低)~4(最高)。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"level\":{\"type\":\"integer\",\"description\":\"音量0~4\"}},"
        "\"required\":[\"level\"]}",
    .execute = tool_robot_set_volume_execute,
};

const daima_tool_t *tool_robot_set_volume_definition(void) { return &s_set_volume; }
