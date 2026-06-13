#include "channel_policy.h"

#include <stdio.h>
#include <string.h>

#include "drivers/channel/vector/vector_channel.h"
#include "log.h"
#include "drivers/pet/pet_event.h"

static const char *TAG = "agent_channel_policy";

static void append_voice_policy(char *prompt, size_t size, size_t off)
{
    int n = snprintf(
        prompt + off, size - off,
        "\n### 通道附加要求\n"
        "- 回答要尽量简短，控制在 1-2 句。\n"
        "- 不要展开长段落，不要罗列过多细节。\n"
        "- 避免使用复杂格式（如长列表/多级标题）。\n");

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
        return;
    }

    uint16_t dir = vector_channel_get_mic_direction();
    if (dir <= 12) {
        static const char *dir_labels[] = {
            "正前方", "右前方", "右方", "右后方", "后方", "后方", "左后方",
            "左方", "左方", "左前方", "前方", "前方", "正上方"
        };
        DAIMA_LOGI(TAG, "Voice context: dir=%d(%s) deg=%d°",
                   dir, dir_labels[dir], (int)dir * 30);
        int dn = snprintf(
            prompt + off + (size_t)n, size - off - (size_t)n,
            "- 当前对用户说话声音来源方向: %d (%s, 角度约 %d°)\n",
            dir, dir_labels[dir], (int)dir * 30);
        if (dn < 0 || (size_t)dn >= (size - off - (size_t)n)) {
            prompt[size - 1] = '\0';
            return;
        }
        n += dn;
    }

    vector_sensor_snapshot_t ss;
    vector_channel_get_sensor_snapshot(&ss);
    if (ss.prox_distance_mm > 0) {
        int dn = snprintf(
            prompt + off + (size_t)n, size - off - (size_t)n,
            "- 前方障碍物检测: %s (距离 %umm), %s\n",
            ss.prox_found_object ? "有" : "无",
            ss.prox_distance_mm,
            ss.prox_unobstructed ? "无遮挡" : "有遮挡");
        if (dn >= 0) {
            n += dn;
        }
    }
    if (ss.cliff_detected) {
        snprintf(
            prompt + off + (size_t)n, size - off - (size_t)n,
            "- 桌面边缘检测: 检测到边缘！靠近桌边，谨慎移动。\n");
    }
    if (ss.head_angle_deg != 0.0f) {
        snprintf(
            prompt + off + (size_t)n, size - off - (size_t)n,
            "- 头部俯仰角: %.0f°, 举升高度: %.0fmm (status=0x%04X)\n",
            ss.head_angle_deg, 0.0f, ss.robot_status);
    }
}

static void append_feishu_policy(char *prompt, size_t size, size_t off)
{
    int n = snprintf(
        prompt + off, size - off,
        "\n### 通道附加要求\n"
        "- 当前输出会被包装成固定的飞书 JSON 2.0 card；你只需要写好正文 markdown，不要假设代码会替你智能挑标题或配色。\n"
        "- 优先短段落、短列表、清晰小节；避免超长开场白、过宽表格和大段代码。\n"
        "- 若任务明显需要飞书友好的排版风格，可先用 `skills` 的 `action=view` 读取 `channels/feishu/feishu-card-writer`。\n");

    if (n < 0 || (size_t)n >= (size - off)) {
        prompt[size - 1] = '\0';
    }
}

void agent_channel_policy_append(char *prompt, size_t size, const daima_msg_t *msg)
{
    if (!prompt || size == 0 || !msg) {
        return;
    }

    size_t off = strnlen(prompt, size - 1);
    if (off >= size - 1) {
        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_VOICE) == 0) {
        append_voice_policy(prompt, size, off);
        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_FEISHU) == 0) {
        append_feishu_policy(prompt, size, off);
        return;
    }

    if (strcmp(msg->channel, DAIMA_CHAN_PET) == 0) {
        size_t next_off = pet_append_channel_policy_prompt(prompt, size, off);
        if (next_off >= size - 1) {
            prompt[size - 1] = '\0';
        }
    }
}
