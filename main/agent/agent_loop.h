/* 智能体主循环接口。 */

#pragma once

#include "daima_err.h"

/**
 * 初始化智能体主循环。
 */
daima_err_t agent_loop_init(void);

/**
 * 启动智能体主循环任务（在 Core 1 上运行）。
 * 从入站队列取消息，调用大模型 API，并推送到出站队列。
 */
daima_err_t agent_loop_start(void);
