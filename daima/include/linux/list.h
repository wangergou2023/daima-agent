#pragma once

#include "linux/kernel.h"

struct list_head { struct list_head *next, *prev; };

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head)
{
    __list_add(new, head, head->next);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)
#define list_for_each_entry(pos, head, member, type) \
    for (pos = list_first_entry(head, type, member); \
         &pos->member != (head); \
         pos = list_entry(pos->member.next, type, member))
#define list_for_each_entry_safe(pos, n, head, member, type) \
    for (pos = list_first_entry(head, type, member), \
         n = list_entry(pos->member.next, type, member); \
         &pos->member != (head); \
         pos = n, n = list_entry(n->member.next, type, member))
