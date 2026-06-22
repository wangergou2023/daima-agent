/* 工具执行回复恢复入口。 */
#pragma once

#include <stdbool.h>

/* 非阻塞轮询执行核回复；有可处理回复时返回 true。 */
bool agent_turn_resume_poll(void);
