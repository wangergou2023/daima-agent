#include "agent/agent_turn_prepare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent/agent_prompt_debug.h"
#include "agent/agent_turn_common.h"
#include "agent/context_builder.h"
#include "llm/llm_proxy.h"
#include "memory/session_store.h"
#include "pet/pet_event.h"
#include "daima_config.h"
#include "daima_env.h"
#include "daima_log.h"
#include "channels/vector/vector_channel.h"
#ifdef DAIMA_ENABLE_VISION
#include "vision/vision_capture.h"
#endif

static const char *TAG = "agent_prepare";

static char *build_current_turn_content(const daima_msg_t *msg)
{
    const char *source = agent_msg_source_or_default(msg);
    const char *content = (msg && msg->content) ? msg->content : "";

    if (!agent_msg_is_synthetic_event(msg)) {
        return strdup(content);
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_CRON) == 0) {
        const char *fmt =
            "这是系统注入的定时提醒事件，不是用户刚刚发送的新消息。\n"
            "事件来源：cron\n"
            "处理要求：若提醒已到点，请直接自然地向用户发出提醒；"
            "不要把这段内容当成用户回复，也不要否认之前已经成功设置的提醒。\n\n"
            "提醒内容：%s";
        size_t need = snprintf(NULL, 0, fmt, content) + 1;
        char *buf = calloc(1, need);
        if (!buf) {
            return NULL;
        }
        snprintf(buf, need, fmt, content);
        return buf;
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_HEARTBEAT) == 0) {
        const char *fmt =
            "这是系统触发的后台巡检事件，不是用户刚刚发送的新消息。\n"
            "事件来源：heartbeat\n"
            "请把下面内容当作系统任务说明执行；若无需用户感知，就不要假装这是用户在说话。\n\n"
            "任务内容：%s";
        size_t need = snprintf(NULL, 0, fmt, content) + 1;
        char *buf = calloc(1, need);
        if (!buf) {
            return NULL;
        }
        snprintf(buf, need, fmt, content);
        return buf;
    }

    if (strcmp(source, DAIMA_MSG_SOURCE_INTERNAL) == 0) {
        return strdup(
            "这是内部控制事件，不是用户消息。\n"
            "不要把它当成对话内容，也不要向用户复述任何内部载荷。"
        );
    }

    return strdup(content);
}

#ifdef DAIMA_ENABLE_VISION
static cJSON *build_user_vision_content(const char *text, const char *image_path)
{
    char local_path[256] = {0};
    bool cleanup_local_path = false;

    if (image_path && image_path[0]) {
        snprintf(local_path, sizeof(local_path), "%s", image_path);
    } else {
#ifdef BUILD_FOR_MIPS
        daima_err_t cap_err = vision_capture_jpeg(NULL, local_path, sizeof(local_path));
        if (cap_err != DAIMA_OK) {
            return NULL;
        }
        cleanup_local_path = true;
#else
        (void)text;
        return NULL;
#endif
    }

    llm_image_content_t img = {0};
    daima_err_t read_err = llm_image_read_file(local_path, &img);
    if (read_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Failed to read image for multimodal request: %s (%s)",
                 local_path, daima_err_to_name(read_err));
        if (cleanup_local_path) {
            unlink(local_path);
        }
        return NULL;
    }

    cJSON *content = llm_create_multimodal_content(text, &img, 1);
    llm_image_content_free(&img);
    DAIMA_LOGI(TAG, "Attached image to multimodal request: %s", local_path);

    if (cleanup_local_path) {
        const char *keep = daima_env_get("DAIMA_VISION_KEEP_SNAPSHOT");
        if (!keep || !keep[0]) {
            unlink(local_path);
        }
    }

    return content;
}
#endif

static void append_turn_context_prompt(char *prompt, size_t size, const daima_msg_t *msg)
{
    if (!prompt || size == 0 || !msg) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    const char *source = agent_msg_source_or_default(msg);
    const char *kind = agent_msg_is_synthetic_event(msg) ? "系统触发事件" : "用户新消息";
    int n = snprintf(
        prompt + off, size - off,
        "\n## 当前轮运行时上下文\n\n"
        "### 当前消息\n"
        "- 来源通道: %s\n"
        "- 来源 chat_id: %s\n"
        "- 消息来源类型: %s\n"
        "- 当前消息性质: %s\n"
        "- 若本轮使用 cron_add 发回当前会话，请设置 channel 与 chat_id 为来源值。\n",
        msg->channel[0] ? msg->channel : "(unknown)",
        msg->chat_id[0] ? msg->chat_id : "(empty)",
        source,
        kind);
    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

static void append_channel_policy_prompt(char *prompt, size_t size, const daima_msg_t *msg)
{
    if (!prompt || size == 0 || !msg) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_VOICE) == 0) {
        int n = snprintf(
            prompt + off, size - off,
            "\n### 通道附加要求\n"
            "- 回答要尽量简短，控制在 1-2 句。\n"
            "- 不要展开长段落，不要罗列过多细节。\n"
            "- 避免使用复杂格式（如长列表/多级标题）。\n");

        if (n < 0 || (size_t)n >= (size - off)) {
            prompt[size - 1] = '\0';
        }

        /* 声源方向 + 传感器 */
        {
            uint16_t dir = vector_channel_get_mic_direction();
            if (dir <= 12) {
                static const char *dir_labels[] = {"正前方","右前方","右方","右后方","后方","后方","左后方","左方","左方","左前方","前方","前方","正上方"};
                DAIMA_LOGI(TAG, "Voice context: dir=%d(%s) deg=%d°", dir, dir_labels[dir], (int)dir * 30);
                int dn = snprintf(prompt + off + n, size - off - (size_t)n,
                    "- 当前对用户说话声音来源方向: %d (%s, 角度约 %d°)\n",
                    dir, dir_labels[dir], (int)dir * 30);
                if (dn < 0 || (size_t)dn >= (size - off - (size_t)n)) {
                    prompt[size - 1] = '\0';
                }
                n += dn;
            }

            vector_sensor_snapshot_t ss;
            vector_channel_get_sensor_snapshot(&ss);
            if (ss.prox_distance_mm > 0) {
                int dn = snprintf(prompt + off + n, size - off - (size_t)n,
                    "- 前方障碍物检测: %s (距离 %umm), %s\n",
                    ss.prox_found_object ? "有" : "无",
                    ss.prox_distance_mm,
                    ss.prox_unobstructed ? "无遮挡" : "有遮挡");
                if (dn >= 0) n += dn;
            }
            if (ss.cliff_detected) {
                snprintf(prompt + off + n, size - off - (size_t)n,
                    "- 桌面边缘检测: 检测到边缘！靠近桌边，谨慎移动。\n");
            }
            if (ss.head_angle_deg != 0.0f) {
                int dn = snprintf(prompt + off + n, size - off - (size_t)n,
                    "- 头部俯仰角: %.0f°, 举升高度: %.0fmm (status=0x%04X)\n",
                    ss.head_angle_deg, 0.0f, ss.robot_status);
                if (dn >= 0) n += dn;
            }
        }

        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_FEISHU) == 0) {
        int n = snprintf(
            prompt + off, size - off,
            "\n### 通道附加要求\n"
            "- 当前输出会被包装成固定的飞书 JSON 2.0 card；你只需要写好正文 markdown，不要假设代码会替你智能挑标题或配色。\n"
            "- 优先短段落、短列表、清晰小节；避免超长开场白、过宽表格和大段代码。\n"
            "- 若任务明显需要飞书友好的排版风格，可先用 `skill_view` 读取 `channels/feishu/feishu-card-writer`。\n");

        if (n < 0 || (size_t)n >= (size - off)) {
            prompt[size - 1] = '\0';
        }
        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_PET) == 0) {
        size_t next_off = pet_append_channel_policy_prompt(prompt, size, off);
        if (next_off >= size - 1) {
            prompt[size - 1] = '\0';
        }
    }
}

static void append_session_facts_prompt(char *prompt, size_t size, const char *chat_id)
{
    if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
        return;
    }

    char facts_buf[2048];
    if (session_store_read_facts(chat_id, facts_buf, sizeof(facts_buf)) != DAIMA_OK || !facts_buf[0]) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
    int n = snprintf(
        prompt + off, size - off,
        "%s### 稳定事实卡片\n"
        "以下内容是从更早轮次中提炼出的稳定偏好、约束、已确认决定。\n"
        "把它们当作长期有效的上下文；若与用户当前这轮明确新指令冲突，以当前新指令为准。\n\n"
        "%s\n",
        has_session_reference ? "\n" : "\n## 会话参考\n\n",
        facts_buf);

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

static void append_session_summary_prompt(char *prompt, size_t size, const char *chat_id)
{
    if (!prompt || size == 0 || !chat_id || !chat_id[0]) {
        return;
    }

    char summary_buf[4096];
    if (session_store_read_summary(chat_id, summary_buf, sizeof(summary_buf)) != DAIMA_OK || !summary_buf[0]) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    bool has_session_reference = strstr(prompt, "\n## 会话参考\n") != NULL;
    int n = snprintf(
        prompt + off, size - off,
        "%s### 最近一次上下文压缩摘要\n"
        "以下内容是对更早对话的结构化交接总结，用来帮助延续上下文。\n"
        "它不是新的用户输入；如果与当前这轮的明确要求冲突，以当前这轮为准。\n\n"
        "%s\n",
        has_session_reference ? "\n" : "\n## 会话参考\n\n",
        summary_buf);

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

daima_err_t agent_turn_prepare(
    const daima_msg_t *msg,
    char *system_prompt,
    size_t system_prompt_size,
    char *history_json,
    size_t history_json_size,
    cJSON **out_messages)
{
    if (!msg || !system_prompt || system_prompt_size == 0 || !history_json || history_json_size == 0 || !out_messages) {
        return DAIMA_ERR_INVALID_ARG;
    }

    *out_messages = NULL;

    context_build_system_prompt_for_channel(msg->channel, system_prompt, system_prompt_size);
    append_session_summary_prompt(system_prompt, system_prompt_size, msg->chat_id);
    append_session_facts_prompt(system_prompt, system_prompt_size, msg->chat_id);
    append_turn_context_prompt(system_prompt, system_prompt_size, msg);
    append_channel_policy_prompt(system_prompt, system_prompt_size, msg);
    context_fix_truncated_utf8(system_prompt, strnlen(system_prompt, system_prompt_size));

    agent_prompt_dump_snapshot(msg, system_prompt);
    DAIMA_LOGI(TAG, "LLM turn context: channel=%s chat_id=%s source=%s",
              msg->channel, msg->chat_id, agent_msg_source_or_default(msg));

    session_store_get_history_json(msg->chat_id, history_json, history_json_size, DAIMA_AGENT_MAX_HISTORY);

    cJSON *messages = cJSON_Parse(history_json);
    if (!messages) messages = cJSON_CreateArray();
    if (!messages) {
        return DAIMA_ERR_NO_MEM;
    }

    cJSON *turn_msg = cJSON_CreateObject();
    if (!turn_msg) {
        cJSON_Delete(messages);
        return DAIMA_ERR_NO_MEM;
    }
    const char *role = agent_msg_role_for_current_turn(msg);
    char *current_content = build_current_turn_content(msg);
    if (!current_content) {
        cJSON_Delete(turn_msg);
        cJSON_Delete(messages);
        return DAIMA_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(turn_msg, "role", role);
#ifdef DAIMA_ENABLE_VISION
    if (strcmp(role, "user") == 0) {
        cJSON *vision_content = build_user_vision_content(msg->content, msg->image_path);
        if (vision_content) {
            cJSON_AddItemToObject(turn_msg, "content", vision_content);
        } else if (msg->image_path && msg->image_path[0]) {
            cJSON_AddStringToObject(
                turn_msg,
                "content",
                "用户发送了一张图片，但当前这次请求没有成功附带图片内容。不要臆测图片细节；请明确说明当前无法读取这张图片，并提示用户稍后重试。");
        } else {
            cJSON_AddStringToObject(turn_msg, "content", current_content);
        }
    } else {
        cJSON_AddStringToObject(turn_msg, "content", current_content);
    }
#else
    cJSON_AddStringToObject(turn_msg, "content", current_content);
#endif
    free(current_content);
    cJSON_AddItemToArray(messages, turn_msg);

    *out_messages = messages;
    return DAIMA_OK;
}
