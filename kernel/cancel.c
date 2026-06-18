/* 协同取消令牌实现。
 * 基于 generation token 模式：每个 chat_id 的每个 turn 分配递增的 token，
 * 取消时递增 generation 令当前 token 失效。线程安全（pthread_mutex）。
 * TLS 存储绑定当前线程的 turn 上下文，避免每次检查都传参。 */

#include "cancel.h"

#include "linux/printk.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"
#define CANCEL_SLOT_MAX 32	/* 全局 slot 表容量（支持 32 个并发会话） */

/* 单个取消槽位：关联一个 chat_id */
typedef struct {
	char chat_id[64];	/* 会话标识 */
	uint64_t generation;	/* 当前 generation 编号（递增） */
	bool cancelled;		/* 是否被取消 */
} cancel_slot_t;

/* 全局状态：互斥锁保护的 slot 表 */
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static cancel_slot_t s_slots[CANCEL_SLOT_MAX];

/* 线程本地存储：当前线程绑定的 turn 上下文（避免频繁传参） */
static __thread char s_thread_chat_id[64];
static __thread uint64_t s_thread_token;

/* 在 slot 表中查找指定 chat_id（调用前必须持有锁） */
static cancel_slot_t *find_slot_locked(const char *chat_id)
{
	if (!chat_id || !chat_id[0]) {
		return NULL;	/* 空 chat_id 视为无效 */
	}

	for (int i = 0; i < CANCEL_SLOT_MAX; i++) {
		if (s_slots[i].chat_id[0] && strcmp(s_slots[i].chat_id, chat_id) == 0) {
			return &s_slots[i];
		}
	}
	return NULL;
}

/* 查找指定 chat_id 的槽位，不存在则创建（LRU 淘汰策略） */
static cancel_slot_t *find_or_create_slot_locked(const char *chat_id)
{
	if (!chat_id || !chat_id[0]) {
		return NULL;
	}

	cancel_slot_t *slot = find_slot_locked(chat_id);
	if (slot) {
		return slot;	/* 已存在，直接返回 */
	}

	/* 查找空槽位 */
	cancel_slot_t *empty = NULL;
	for (int i = 0; i < CANCEL_SLOT_MAX; i++) {
		if (s_slots[i].chat_id[0] == '\0') {
			empty = &s_slots[i];
			break;
		}
	}

	/* 无空位时淘汰第一个槽位（LRU 近似） */
	slot = empty ? empty : &s_slots[0];
	if (!empty && slot->chat_id[0]) {
		pr_warn("Cancel slot table full, evicting chat=%s", slot->chat_id);
	}
	strscpy(slot->chat_id, chat_id, sizeof(slot->chat_id));
	slot->generation = 0;
	slot->cancelled = false;
	return slot;
}

/* 开始新 turn：递增 generation，清除 cancelled 标记，返回新 token */
uint64_t agent_cancel_begin_turn(const char *chat_id)
{
	pthread_mutex_lock(&s_mutex);
	cancel_slot_t *slot = find_or_create_slot_locked(chat_id);
	if (slot) {
		slot->generation++;
		if (slot->generation == 0)
			slot->generation = 1;	/* generation 永不为 0（0 表示无效） */
		slot->cancelled = false;	/* 新 turn 初始未取消 */
	}
	uint64_t token = slot ? slot->generation : 0;
	pthread_mutex_unlock(&s_mutex);
	return token;
}

/* 请求取消：递增 generation 使当前 token 失效，设置 cancelled 标记 */
void agent_cancel_request(const char *chat_id, const char *reason)
{
	pthread_mutex_lock(&s_mutex);
	cancel_slot_t *slot = find_or_create_slot_locked(chat_id);
	if (slot) {
		slot->cancelled = true;
		slot->generation++;		/* 递增 generation 使所有旧 token 失效 */
		if (slot->generation == 0)
			slot->generation = 1;	/* generation 永不为 0 */
	}
	pthread_mutex_unlock(&s_mutex);

	pr_info("Cancel requested for chat=%s reason=%s", chat_id, reason && reason[0] ? reason : "-");
}

/* 检查 token 是否仍然有效（generation 匹配且未被 cancelled） */
bool agent_cancel_is_cancelled(const char *chat_id, uint64_t token)
{
	if (!chat_id || !chat_id[0]) {
		return false;	/* 无效 chat_id 视为未取消 */
	}

	pthread_mutex_lock(&s_mutex);
	cancel_slot_t *slot = find_slot_locked(chat_id);
	/* 取消条件：slot 不存在、generation 不匹配、或 cancelled 标记为 true */
	bool cancelled = slot && (slot->generation != token || slot->cancelled);
	pthread_mutex_unlock(&s_mutex);
	return cancelled;
}

/* 将当前线程绑定到指定 turn 的取消上下文（TLS 存储） */
void agent_cancel_enter_current_turn(const char *chat_id, uint64_t token)
{
	strscpy(s_thread_chat_id, chat_id ? chat_id : "", sizeof(s_thread_chat_id));
	s_thread_token = token;
}

/* 解绑当前线程的取消上下文 */
void agent_cancel_leave_current_turn(void)
{
	s_thread_chat_id[0] = '\0';
	s_thread_token = 0;
}

/* 检查当前线程绑定的 turn 是否被取消（使用 TLS 缓存，无需传参） */
bool agent_cancel_current_thread_cancelled(void)
{
	if (!s_thread_chat_id[0]) {
		return false;	/* 未绑定 turn 上下文 */
	}
	return agent_cancel_is_cancelled(s_thread_chat_id, s_thread_token);
}
