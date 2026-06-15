/* 记忆存储接口。 */

#pragma once

#include "err.h"
#include <stddef.h>

/**
 * 初始化记忆存储。确保 SPIFFS 目录存在。
 */
err_t memory_store_init(void);

/**
 * 读取长期记忆（MEMORY.md）到缓冲区。
 * @return 成功返回 0，文件不存在返回 ERR_NOT_FOUND
 */
err_t memory_read_long_term(char *buf, size_t size);

/**
 * 写入长期记忆（MEMORY.md）。
 */
err_t memory_write_long_term(const char *content);

/**
 * 追加一条笔记到今日的每日记忆文件（YYYY-MM-DD.md）。
 */
err_t memory_append_today(const char *note);

/**
 * 读取最近的每日记忆（最近 N 天）到缓冲区。
 * @param days  回溯天数（默认 3）
 */
err_t memory_read_recent(char *buf, size_t size, int days);
