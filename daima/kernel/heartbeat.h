/* 心跳服务接口。 */

#pragma once

#include "err.h"
#include <stdbool.h>

/**
 * 初始化心跳服务（记录就绪状态）。
 */
daima_err_t heartbeat_init(void);

/**
 * 启动心跳定时器。周期性检查 HEARTBEAT.md，
 * 若发现可执行任务则向智能体发送提示。
 */
daima_err_t heartbeat_start(void);

/**
 * 停止并删除心跳定时器。
 */
void heartbeat_stop(void);

/**
 * 手动触发一次心跳检查（用于 CLI 测试）。
 * 若触发了智能体提示则返回 true，未发现任务则返回 false。
 */
bool heartbeat_trigger(void);
