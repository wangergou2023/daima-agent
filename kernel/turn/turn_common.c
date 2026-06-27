/* Turn 公共函数：消息来源判断、角色解析、chat_id 转安全 slug、消息清理等工具函数。
 * 所有 turn 阶段（prepare/run/exec/finish/persist）共享这里的基础工具。 */

#include "turn_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "autoconf.h"
#include "env.h"
#include "linux/slab.h"

/** 从环境变量读取整数值，不存在或非数字时返回默认值。 */
int agent_env_int_or_default(const char *name, int fallback)
{
    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        return fallback;
    }
    char *end = NULL;
    long val = strtol(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) {
        return fallback;
    }
    return (int)val;
}

/** 从环境变量读取布尔值，支持 1/true/yes（真）和 0/false/no（假），其他返回默认值。 */
bool agent_env_bool_or_default(const char *name, bool fallback)
{
    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        return fallback;
    }
    if (strcmp(raw, "1") == 0 || strcasecmp(raw, "true") == 0 || strcasecmp(raw, "yes") == 0) {
        return true;
    }
    if (strcmp(raw, "0") == 0 || strcasecmp(raw, "false") == 0 || strcasecmp(raw, "no") == 0) {
        return false;
    }
    return fallback;
}


/** 获取消息来源字段，未设置时默认返回 "user"。 */
const char *agent_msg_source_or_default(const struct message *msg)
{
    if (!msg) {
        return MSG_SOURCE_USER;
    }
    if (msg->source[0]) {
        return msg->source;
    }
    return MSG_SOURCE_USER;
}

/** 判断消息是否为内部控制消息（非用户、非 cron、非 heartbeat）。 */
bool agent_msg_is_internal_control(const struct message *msg)
{
    return strcmp(agent_msg_source_or_default(msg), MSG_SOURCE_INTERNAL) == 0;
}

/** 判断消息是否为系统合成事件（cron、heartbeat、internal 或 system 通道）。 */
bool agent_msg_is_synthetic_event(const struct message *msg)
{
    const char *source = agent_msg_source_or_default(msg);
    if (strcmp(source, MSG_SOURCE_CRON) == 0 ||
        strcmp(source, MSG_SOURCE_HEARTBEAT) == 0 ||
        strcmp(source, MSG_SOURCE_DELEGATE) == 0 ||
        strcmp(source, MSG_SOURCE_INTERNAL) == 0) {
        return true;
    }
    return msg && strcmp(msg->channel, CHAN_SYSTEM) == 0;
}

/** 返回当前轮次消息的 LLM 角色：合成事件用 "system"，普通消息用 "user"。 */
const char *agent_msg_role_for_current_turn(const struct message *msg)
{
    return agent_msg_is_synthetic_event(msg) ? "system" : "user";
}

/** 返回入站消息的会话存储角色。
 *  user 消息存为 "user"，internal 不存，其他来源存为 "system"。
 *  @return 角色字符串或 NULL（无需存储） */
const char *agent_session_role_for_inbound_msg(const struct message *msg)
{
    if (!msg || !msg->content || !msg->content[0]) {
        return NULL;
    }

    const char *source = agent_msg_source_or_default(msg);
    if (strcmp(source, MSG_SOURCE_USER) == 0) {
        return "user";
    }
    if (strcmp(source, MSG_SOURCE_INTERNAL) == 0) {
        return NULL;    /* 内部控制消息不记录到会话历史 */
    }
    return "system";
}

/** 将 chat_id 转换为文件系统安全标识符：仅保留 a-zA-Z0-9_-，其他字符替换为 '_'。
 *  @param chat_id  原始 chat_id
 *  @param buf      输出缓冲区
 *  @param size     缓冲区大小 */
void agent_chat_id_to_slug(const char *chat_id, char *buf, size_t size)
{
    if (!buf || size == 0) {
        return;
    }
    buf[0] = '\0';
    if (!chat_id || !chat_id[0]) {
        snprintf(buf, size, "unknown");
        return;
    }

    size_t off = 0;
    for (const char *p = chat_id; *p && off + 1 < size; ++p) {
        char ch = *p;
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-') {
            buf[off++] = ch;
        } else {
            buf[off++] = '_';
        }
    }
    buf[off] = '\0';
    if (off == 0) {
        snprintf(buf, size, "unknown");
    }
}

/** 释放入站消息占用的内存资源及临时图片文件。 */
void agent_cleanup_inbound_msg(struct message *msg)
{
    if (!msg) {
        return;
    }

    kfree(msg->content);
    msg->content = NULL;

    if (msg->image_path) {
        unlink(msg->image_path);
    }
    kfree(msg->image_path);
    msg->image_path = NULL;
}

/** 释放出站消息占用的内存（content + reasoning）。 */
void agent_cleanup_outbound_msg(struct message *msg)
{
    if (!msg) {
        return;
    }

    kfree(msg->content);
    msg->content = NULL;
    kfree(msg->reasoning);
    msg->reasoning = NULL;
}
