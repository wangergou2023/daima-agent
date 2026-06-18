/* 通道运行时分发：统一封装普通出站文本的发送逻辑。
 * 根据消息的 channel 字段选择合适的通道驱动，
 * 将最终回复文本推送到对应用户/群聊。 */

#pragma once

#include "bus.h"
#include "err.h"

/* 将出站消息分发到对应通道（飞书/WebSocket/system 等） */
err_t channel_runtime_dispatch_outbound(const struct message *msg);
