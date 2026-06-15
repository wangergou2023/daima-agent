/* 系统提示与上下文构建接口。 */

#pragma once

#include "err.h"
#include <stddef.h>

/**
 * 基于引导文件（SOUL.md、USER.md）和记忆上下文
 *（MEMORY.md + 最近的每日笔记）构建系统提示。
 *
 * @param buf   输出缓冲区（由调用方分配，建议 CONTEXT_BUF_SIZE）
 * @param size  缓冲区大小
 */
err_t context_build_system_prompt_for_channel(const char *channel, char *buf, size_t size);

err_t context_build_system_prompt(char *buf, size_t size);

/**
 * 修复缓冲区末尾可能被截断的 multi-byte UTF-8 序列。
 */
void context_fix_truncated_utf8(char *buf, size_t len);
