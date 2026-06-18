/* 内核双向循环链表：list_head、添加/删除/遍历宏。 */

#pragma once

#include "linux/kernel.h"

/* 链表头节点（自身为双向循环，next 和 prev 指向自身表示空链表） */
struct list_head { struct list_head *next, *prev; };

/* 静态初始化一个链表节点 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }
/* 声明并初始化一个链表头 */
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

/**
 * 初始化链表头（指向自身形成空循环链表）。
 * @param list  链表头指针
 */
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/* 内部：在 prev 和 next 之间插入 new_entry */
static inline void __list_add(struct list_head *new_entry,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new_entry;
	new_entry->next = next;
	new_entry->prev = prev;
	prev->next = new_entry;
}

/**
 * 在链表头后添加节点（头部插入）。
 * @param new_entry  新节点
 * @param head       链表头
 */
static inline void list_add(struct list_head *new_entry, struct list_head *head)
{
	__list_add(new_entry, head, head->next);
}

/* 内部：删除 prev 和 next 之间的节点 */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * 从链表中删除节点并重新初始化为空节点。
 * @param entry  要删除的节点
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

/* 遍历链表节点本身（不含头节点） */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
/* 通过链表节点 member 反推包含它的结构体 */
#define list_entry(ptr, type, member) container_of(ptr, type, member)
/* 获取链表中第一个 entry（通过 member 字段） */
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)
/* 遍历链表 entry（通过指定的 member 字段访问包含它的结构体） */
#define list_for_each_entry(pos, head, member, type) \
	for (pos = list_first_entry(head, type, member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, type, member))
/* 安全遍历链表 entry（可在遍历中删除当前节点，n 保存下一个位置） */
#define list_for_each_entry_safe(pos, n, head, member, type) \
	for (pos = list_first_entry(head, type, member), \
	     n = list_entry(pos->member.next, type, member); \
	     &pos->member != (head); \
	     pos = n, n = list_entry(n->member.next, type, member))
