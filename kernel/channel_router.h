/* 通道路由器接口。
 * 启动通道消息路由服务，从各通道总线的入站队列读取消息，
 * 进行意图分类后分发到 agent 主循环处理。 */

#pragma once

#include "err.h"

/* 启动通道路由器（在 initcall 阶段调用） */
err_t channel_router_start(void);
