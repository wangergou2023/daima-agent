/* 后台自进化复盘：长时记忆提炼与技能更新建议。 */

#pragma once

#include "err.h"

/**
 * 初始化后台学习复盘线程。
 */
err_t learning_review_init(void);

/**
 * 异步调度一次会话复盘。
 * 当前设计只在较复杂的任务后调用，不阻塞当前回复。
 */
err_t learning_review_schedule(const char *chat_id);
