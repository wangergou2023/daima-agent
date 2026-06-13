/* 会话历史上下文压缩接口。 */

#pragma once

#include "err.h"
#include <stdbool.h>

typedef struct cJSON cJSON;

/**
 * 在历史过长时将中间消息压缩为摘要，并回写会话文件。
 *
 * @param chat_id        会话标识
 * @param system_prompt  当前轮系统提示，用于粗略估算上下文大小
 * @param messages_io    输入/输出历史消息数组（仅 user/assistant 字符串消息）
 * @param did_compact    输出：本次是否真的发生压缩
 */
daima_err_t context_compressor_maybe_compact(
    const char *chat_id,
    const char *system_prompt,
    cJSON **messages_io,
    bool *did_compact);

/**
 * 初始化后台压缩线程。
 */
daima_err_t context_compressor_init(void);

/**
 * 异步请求对某个会话做后台压缩。
 * 该调用只入队，不阻塞当前对话。
 */
daima_err_t context_compressor_schedule(const char *chat_id);

/**
 * 仅在当前会话确实达到压缩阈值时才异步入队。
 */
daima_err_t context_compressor_schedule_if_needed(const char *chat_id);
