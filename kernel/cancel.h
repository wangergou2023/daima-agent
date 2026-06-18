/* 协同取消令牌系统。
 * 提供基于 generation token 的协作式取消机制（非强制 kill）。
 * 每个 chat_id 的每次 turn 分配一个递增的 generation token，
 * 执行过程中检查 token 是否仍有效，若被取消则协作退出。
 * 线程安全：内部使用 pthread_mutex 保护全局 slot 表。 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * 开始新的 turn，为该 chat_id 递增 generation 并返回新 token。
 * 同时清除该 chat_id 的 cancelled 标记。
 * @param chat_id  会话标识
 * @return 新 generation token（0 表示 chat_id 无效）
 */
uint64_t agent_cancel_begin_turn(const char *chat_id);

/**
 * 请求取消指定 chat_id 的当前 turn。
 * 递增 generation 使当前 token 失效，并设置 cancelled 标记。
 * @param chat_id  会话标识
 * @param reason   取消原因（用于日志）
 */
void agent_cancel_request(const char *chat_id, const char *reason);

/**
 * 检查指定 chat_id 的 turn 是否已被取消。
 * 当 generation 与 token 不匹配或 cancelled 标记为 true 时返回 true。
 * @param chat_id  会话标识
 * @param token    当前持有的 generation token
 * @return true 表示已被取消，应协作退出
 */
bool agent_cancel_is_cancelled(const char *chat_id, uint64_t token);

/**
 * 将当前线程与指定 turn 的取消上下文绑定（TLS 存储）。
 * 绑定后可通过 agent_cancel_current_thread_cancelled() 快速检查。
 * @param chat_id  会话标识
 * @param token    当前 turn 的 generation token
 */
void agent_cancel_enter_current_turn(const char *chat_id, uint64_t token);

/* 解绑当前线程的取消上下文 */
void agent_cancel_leave_current_turn(void);

/**
 * 检查当前线程绑定的 turn 是否已被取消（使用 TLS 存储的 chat_id/token）。
 * @return true 表示已被取消
 */
bool agent_cancel_current_thread_cancelled(void);
