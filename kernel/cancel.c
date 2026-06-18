#include "cancel.h"

#include "linux/printk.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "linux/kernel.h"
#define CANCEL_SLOT_MAX 32

typedef struct {
    char chat_id[64];
    uint64_t generation;
    bool cancelled;
} cancel_slot_t;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static cancel_slot_t s_slots[CANCEL_SLOT_MAX];

static __thread char s_thread_chat_id[64];
static __thread uint64_t s_thread_token;

static cancel_slot_t *find_slot_locked(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    for (int i = 0; i < CANCEL_SLOT_MAX; i++) {
        if (s_slots[i].chat_id[0] && strcmp(s_slots[i].chat_id, chat_id) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static cancel_slot_t *find_or_create_slot_locked(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }

    cancel_slot_t *slot = find_slot_locked(chat_id);
    if (slot) {
        return slot;
    }

    cancel_slot_t *empty = NULL;
    for (int i = 0; i < CANCEL_SLOT_MAX; i++) {
        if (s_slots[i].chat_id[0] == '\0') {
            empty = &s_slots[i];
            break;
        }
    }

    slot = empty ? empty : &s_slots[0];
    if (!empty && slot->chat_id[0]) {
        pr_warn("Cancel slot table full, evicting chat=%s", slot->chat_id);
    }
    strscpy(slot->chat_id, chat_id, sizeof(slot->chat_id));
    slot->generation = 0;
    slot->cancelled = false;
    return slot;
}

uint64_t agent_cancel_begin_turn(const char *chat_id)
{
    pthread_mutex_lock(&s_mutex);
    cancel_slot_t *slot = find_or_create_slot_locked(chat_id);
    if (slot) {
        slot->generation++;
        if (slot->generation == 0)
            slot->generation = 1;
        slot->cancelled = false;
    }
    uint64_t token = slot ? slot->generation : 0;
    pthread_mutex_unlock(&s_mutex);
    return token;
}

void agent_cancel_request(const char *chat_id, const char *reason)
{
    pthread_mutex_lock(&s_mutex);
    cancel_slot_t *slot = find_or_create_slot_locked(chat_id);
    if (slot) {
        slot->cancelled = true;
        slot->generation++;
        if (slot->generation == 0)
            slot->generation = 1;
    }
    pthread_mutex_unlock(&s_mutex);

    pr_info("Cancel requested for chat=%s reason=%s", chat_id, reason && reason[0] ? reason : "-");
}

bool agent_cancel_is_cancelled(const char *chat_id, uint64_t token)
{
    if (!chat_id || !chat_id[0]) {
        return false;
    }

    pthread_mutex_lock(&s_mutex);
    cancel_slot_t *slot = find_slot_locked(chat_id);
    bool cancelled = slot && (slot->generation != token || slot->cancelled);
    pthread_mutex_unlock(&s_mutex);
    return cancelled;
}

void agent_cancel_enter_current_turn(const char *chat_id, uint64_t token)
{
    strscpy(s_thread_chat_id, chat_id ? chat_id : "", sizeof(s_thread_chat_id));
    s_thread_token = token;
}

void agent_cancel_leave_current_turn(void)
{
    s_thread_chat_id[0] = '\0';
    s_thread_token = 0;
}

bool agent_cancel_current_thread_cancelled(void)
{
    if (!s_thread_chat_id[0]) {
        return false;
    }
    return agent_cancel_is_cancelled(s_thread_chat_id, s_thread_token);
}
