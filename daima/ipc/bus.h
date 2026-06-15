/* 消息总线接口定义。 */

#pragma once

#include "err.h"
#include "intent.h"
#include <stdint.h>

/* 通道标识 */
#define CHAN_WEBSOCKET  "websocket"
#define CHAN_PET        "pet"
#define CHAN_VOICE      "voice"
#define CHAN_FEISHU     "feishu"
#define CHAN_SYSTEM     "system"
#define CHAN_VECTOR     "vector"

/* 消息来源类型 */
#define MSG_SOURCE_USER      "user"
#define MSG_SOURCE_CRON      "cron"
#define MSG_SOURCE_HEARTBEAT "heartbeat"
#define MSG_SOURCE_INTERNAL  "internal"

/* 总线消息类型 */
struct message {
    char channel[16];       /* "websocket", "voice", "feishu", "system" */
    char chat_id[64];       /* 会话 id（WS 客户端/Feishu open_id/chat_id） */
    char source[16];        /* "user", "cron", "heartbeat", "internal" */
    char *content;          /* 堆分配的消息文本（调用方需释放） */
    char *reasoning;        /* 可选：助手思考过程（调用方需释放） */
    char *image_path;       /* 可选：入站图片的本地缓存路径（调用方需释放） */
    enum intent intent;
};

/**
 * 初始化消息总线（入站 + 出站队列）。
 */
err_t message_bus_init(void);

/**
 * 将消息推入入站队列（指向智能体主循环）。
 * 总线接管 msg->content / msg->image_path 的所有权。
 */
err_t message_bus_push_inbound(const struct message *msg);

/**
 * 从入站队列取出消息（阻塞）。
 * 使用完后调用方需释放 msg->content / msg->image_path。
 */
err_t message_bus_pop_inbound(struct message *msg, uint32_t timeout_ms);

/**
 * 将消息推入出站队列（指向各通道）。
 * 总线接管 msg->content / msg->reasoning 的所有权。
 */
err_t message_bus_push_outbound(const struct message *msg);

/**
 * 从出站队列取出消息（阻塞）。
 * 使用完后调用方需释放 msg->content / msg->reasoning。
 */
err_t message_bus_pop_outbound(struct message *msg, uint32_t timeout_ms);
