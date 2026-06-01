/* Vector 音频工具: play_pcm, say_text, set_volume */
#include "tools/tool_vector_common.h"
#include "tools/tool_registry.h"
#include "channels/vector/vector_channel.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "daima_log.h"

static const char *TAG = "tool_vector_audio";

static mcp_client_t *get_mcp(char *output, size_t size) {
    mcp_client_t *m = vector_channel_get_mcp();
    if (!m) snprintf(output, size, "错误：Vector 机器人未连接");
    return m;
}

/* ---- Play PCM ---- */
static daima_err_t tool_robot_play_pcm_execute(const char *input_json, char *output, size_t output_size)
{
    mcp_client_t *mcp = get_mcp(output, output_size);
    if (!mcp) return DAIMA_FAIL;

    cJSON *in = cJSON_Parse(input_json);
    if (!in) { snprintf(output, output_size, "错误：无效 JSON"); return DAIMA_ERR_INVALID_ARG; }

    cJSON *b64 = cJSON_GetObjectItem(in, "pcm_base64");
    cJSON *sr  = cJSON_GetObjectItem(in, "sample_rate");
    cJSON *vol = cJSON_GetObjectItem(in, "volume");

    if (!b64 || !cJSON_IsString(b64)) {
        snprintf(output, output_size, "错误：缺少 pcm_base64 参数");
        cJSON_Delete(in);
        return DAIMA_ERR_INVALID_ARG;
    }
    double sample_rate = sr && cJSON_IsNumber(sr) ? sr->valuedouble : 16000.0;
    double volume = vol && cJSON_IsNumber(vol) ? vol->valuedouble : 50.0;

    /* 构造 args — 注意 base64 中不含需转义的引号 */
    char args[8192];
    snprintf(args, sizeof(args),
             "{\"pcm_base64\":\"%s\",\"sample_rate\":%.0f,\"volume\":%.0f}",
             b64->valuestring, sample_rate, volume);
    cJSON_Delete(in);
    return mcp_client_call_tool(mcp, "robot_play_pcm", args, output, output_size);
}

static const daima_tool_t s_play_pcm = {
    .name = "robot_play_pcm",
    .description = "通过 Vector 扬声器播放原始 PCM 音频。pcm_base64: base64编码的16bit signed PCM数据。sample_rate: 采样率(默认16000)。volume: 音量0-100(默认50)。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pcm_base64\":{\"type\":\"string\",\"description\":\"base64编码的PCM音频数据\"},"
        "\"sample_rate\":{\"type\":\"number\",\"description\":\"采样率Hz，默认16000\"},"
        "\"volume\":{\"type\":\"number\",\"description\":\"音量0-100，默认50\"}"
        "},\"required\":[\"pcm_base64\"]}",
    .execute = tool_robot_play_pcm_execute,
};

const daima_tool_t *tool_robot_play_pcm_definition(void) { return &s_play_pcm; }

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

    char args[64];
    snprintf(args, sizeof(args), "{\"level\":%d}", level);
    return mcp_client_call_tool(mcp, "robot_set_volume", args, output, output_size);
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
