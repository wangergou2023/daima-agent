#include "pet/pet_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PET_CHAT_PREFIX = "pet_";

char *pet_build_action_prompt(const char *action, const char *pet_id)
{
    const char *pet = (pet_id && pet_id[0]) ? pet_id : "pet";
    const char *verb = "有人和你互动了";
    if (action && action[0]) {
        if (strcmp(action, PET_ACTION_TAP) == 0 || strcmp(action, PET_ACTION_CLICK) == 0) {
            verb = "用户单击了你一下";
        } else if (strcmp(action, PET_ACTION_DRAG) == 0) {
            verb = "用户把你拎起来拖了拖";
        } else if (strcmp(action, PET_ACTION_DROP) == 0) {
            verb = "用户把你放下来了";
        }
    }

    const char *fmt =
        "这是宠物互动事件，不是主聊天通道的新问题。\n"
        "你是宠物 `%s`，刚刚%s。\n"
        "请用 1 句简短、可爱、像桌宠一样的口吻回应，不要展开成长答案。\n"
        "如果合适，可在句末附加一个隐藏动作标记 `[[pet:state=...]]`。";
    size_t need = snprintf(NULL, 0, fmt, pet, verb) + 1;
    char *buf = calloc(1, need);
    if (!buf) {
        return NULL;
    }
    snprintf(buf, need, fmt, pet, verb);
    return buf;
}

bool pet_build_chat_id(const char *chat_id, char *out, size_t out_size)
{
    if (!chat_id || !chat_id[0] || !out || out_size == 0) {
        return false;
    }

    return snprintf(out, out_size, "%s%s", PET_CHAT_PREFIX, chat_id) < (int)out_size;
}

bool pet_chat_id_to_ws_chat_id(const char *pet_chat_id, char *out, size_t out_size)
{
    size_t prefix_len = strlen(PET_CHAT_PREFIX);

    if (!pet_chat_id || !out || out_size == 0) {
        return false;
    }
    if (strncmp(pet_chat_id, PET_CHAT_PREFIX, prefix_len) != 0 || !pet_chat_id[prefix_len]) {
        return false;
    }

    return snprintf(out, out_size, "%s", pet_chat_id + prefix_len) < (int)out_size;
}

size_t pet_append_channel_policy_prompt(char *prompt, size_t size, size_t offset)
{
    int n = 0;

    if (!prompt || size == 0 || offset >= size - 1) {
        return offset;
    }

    n = snprintf(
        prompt + offset, size - offset,
        "\n### 通道附加要求\n"
        "- 这是 Web 宠物的独立互动通道，不是主聊天窗口里的新问题。\n"
        "- 请像桌宠一样回应：1 句即可，短、轻、可爱，不要展开说明。\n"
        "- 默认不要调用工具，也不要把它写成任务汇报、教程或正式答复。\n"
        "- 如果你想驱动宠物表情或动作，可以在句末附加 `[[pet:state=...]]`。\n");

    if (n < 0) {
        prompt[size - 1] = '\0';
        return offset;
    }
    if ((size_t)n >= size - offset) {
        prompt[size - 1] = '\0';
        return size - 1;
    }
    return offset + (size_t)n;
}
