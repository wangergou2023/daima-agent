/* turn 异步恢复快照存储，简单数组实现。 */
#include "turn_context.h"
#include "linux/core_task.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include <pthread.h>
#include "cjson.h"
#include <string.h>

#define MAX_SNAPSHOTS 8

static struct turn_snapshot s_snapshots[MAX_SNAPSHOTS];
static int s_count = 0;
static pthread_mutex_t s_snapshots_mutex = PTHREAD_MUTEX_INITIALIZER;

static void turn_context_sync_legacy_consumed_slot(struct turn_snapshot *snap)
{
    if (!snap) {
        return;
    }

    if (snap->consumed_delegate_coordinator_id[0] &&
        snap->consumed_delegate_visible_revision > 0) {
        strscpy(snap->consumed_delegate_resumes[0].coordinator_id,
                snap->consumed_delegate_coordinator_id,
                sizeof(snap->consumed_delegate_resumes[0].coordinator_id));
        snap->consumed_delegate_resumes[0].visible_revision =
            snap->consumed_delegate_visible_revision;
    } else if (snap->consumed_delegate_resumes[0].coordinator_id[0] &&
               snap->consumed_delegate_resumes[0].visible_revision > 0) {
        strscpy(snap->consumed_delegate_coordinator_id,
                snap->consumed_delegate_resumes[0].coordinator_id,
                sizeof(snap->consumed_delegate_coordinator_id));
        snap->consumed_delegate_visible_revision =
            snap->consumed_delegate_resumes[0].visible_revision;
    }
}

static void turn_context_record_consumed_delegate_resume(struct turn_snapshot *snap,
                                                         const char *coordinator_id,
                                                         unsigned long visible_revision)
{
    int slot = -1;

    if (!snap || !coordinator_id || !coordinator_id[0] || visible_revision == 0) {
        return;
    }

    for (int i = 0; i < TURN_CONTEXT_CONSUMED_DELEGATE_MAX; i++) {
        if (strcmp(snap->consumed_delegate_resumes[i].coordinator_id, coordinator_id) == 0) {
            slot = i;
            break;
        }
        if (slot < 0 && !snap->consumed_delegate_resumes[i].coordinator_id[0]) {
            slot = i;
        }
    }
    if (slot < 0) {
        memmove(&snap->consumed_delegate_resumes[1],
                &snap->consumed_delegate_resumes[0],
                (TURN_CONTEXT_CONSUMED_DELEGATE_MAX - 1) *
                    sizeof(snap->consumed_delegate_resumes[0]));
        slot = 0;
        memset(&snap->consumed_delegate_resumes[slot], 0,
               sizeof(snap->consumed_delegate_resumes[slot]));
    }

    strscpy(snap->consumed_delegate_resumes[slot].coordinator_id,
            coordinator_id,
            sizeof(snap->consumed_delegate_resumes[slot].coordinator_id));
    if (visible_revision > snap->consumed_delegate_resumes[slot].visible_revision) {
        snap->consumed_delegate_resumes[slot].visible_revision = visible_revision;
    }

    if (slot != 0) {
        struct turn_consumed_delegate_resume entry = snap->consumed_delegate_resumes[slot];
        memmove(&snap->consumed_delegate_resumes[1],
                &snap->consumed_delegate_resumes[0],
                slot * sizeof(snap->consumed_delegate_resumes[0]));
        snap->consumed_delegate_resumes[0] = entry;
    }

    strscpy(snap->consumed_delegate_coordinator_id,
            snap->consumed_delegate_resumes[0].coordinator_id,
            sizeof(snap->consumed_delegate_coordinator_id));
    snap->consumed_delegate_visible_revision =
        snap->consumed_delegate_resumes[0].visible_revision;
}

static bool turn_context_has_consumed_delegate_resume_in_snapshot(
    const struct turn_snapshot *snap,
    const char *coordinator_id,
    unsigned long visible_revision)
{
    if (!snap || !coordinator_id || !coordinator_id[0] || visible_revision == 0) {
        return false;
    }

    for (int i = 0; i < TURN_CONTEXT_CONSUMED_DELEGATE_MAX; i++) {
        if (!snap->consumed_delegate_resumes[i].coordinator_id[0]) {
            continue;
        }
        if (strcmp(snap->consumed_delegate_resumes[i].coordinator_id, coordinator_id) != 0) {
            continue;
        }
        if (snap->consumed_delegate_resumes[i].visible_revision >= visible_revision) {
            return true;
        }
    }

    return strcmp(snap->consumed_delegate_coordinator_id, coordinator_id) == 0 &&
           snap->consumed_delegate_visible_revision >= visible_revision;
}

void turn_context_save(const struct turn_snapshot *snap)
{
    if (!snap || !snap->chat_id[0]) return;

    pthread_mutex_lock(&s_snapshots_mutex);

    /* 先查找已有快照，覆盖 */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, snap->chat_id) == 0) {
            /* 释放旧数据 */
            cJSON_Delete(s_snapshots[i].messages);
            kfree(s_snapshots[i].system_prompt);
            /* 覆盖 */
            s_snapshots[i] = *snap;
            s_snapshots[i].messages = snap->messages ? cJSON_Duplicate(snap->messages, 1) : NULL;
            s_snapshots[i].system_prompt = snap->system_prompt ? strdup(snap->system_prompt) : NULL;
            turn_context_sync_legacy_consumed_slot(&s_snapshots[i]);
            pthread_mutex_unlock(&s_snapshots_mutex);
            return;
        }
    }

    /* 新快照 */
    if (s_count >= MAX_SNAPSHOTS) {
        /* 淘汰最旧的 */
        cJSON_Delete(s_snapshots[0].messages);
        kfree(s_snapshots[0].system_prompt);
        memmove(&s_snapshots[0], &s_snapshots[1], (MAX_SNAPSHOTS - 1) * sizeof(struct turn_snapshot));
        s_count--;
    }

    s_snapshots[s_count] = *snap;
    s_snapshots[s_count].messages = snap->messages ? cJSON_Duplicate(snap->messages, 1) : NULL;
    s_snapshots[s_count].system_prompt = snap->system_prompt ? strdup(snap->system_prompt) : NULL;
    turn_context_sync_legacy_consumed_slot(&s_snapshots[s_count]);
    s_count++;
    pthread_mutex_unlock(&s_snapshots_mutex);
}

bool turn_context_load_copy(const char *chat_id, struct turn_snapshot *out)
{
    if (!chat_id || !chat_id[0] || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) == 0) {
            *out = s_snapshots[i];
            out->messages = s_snapshots[i].messages ? cJSON_Duplicate(s_snapshots[i].messages, 1) : NULL;
            out->system_prompt = s_snapshots[i].system_prompt ? strdup(s_snapshots[i].system_prompt) : NULL;
            pthread_mutex_unlock(&s_snapshots_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return false;
}

void turn_context_remove(const char *chat_id)
{
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) == 0) {
            cJSON_Delete(s_snapshots[i].messages);
            kfree(s_snapshots[i].system_prompt);
            if (i < s_count - 1)
                memmove(&s_snapshots[i], &s_snapshots[i + 1],
                        (s_count - i - 1) * sizeof(struct turn_snapshot));
            s_count--;
            pthread_mutex_unlock(&s_snapshots_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
}

void turn_context_snapshot_cleanup(struct turn_snapshot *snap)
{
    if (!snap) {
        return;
    }
    cJSON_Delete(snap->messages);
    kfree(snap->system_prompt);
    memset(snap, 0, sizeof(*snap));
}

bool turn_context_find_by_task(const char *task_id, char *chat_id, size_t chat_id_size)
{
    if (!task_id || !task_id[0] || !chat_id || chat_id_size == 0) {
        return false;
    }
    chat_id[0] = '\0';
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (s_snapshots[i].pending_task_id[0] &&
            strcmp(s_snapshots[i].pending_task_id, task_id) == 0) {
            strscpy(chat_id, s_snapshots[i].chat_id, chat_id_size);
            pthread_mutex_unlock(&s_snapshots_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return false;
}

bool turn_context_set_pending_request(const char *chat_id,
                                      const char *request_type,
                                      const char *request_id,
                                      const char *prompt_text)
{
    if (!chat_id || !chat_id[0] || !request_type || !request_type[0] ||
        !request_id || !request_id[0]) {
        return false;
    }

    bool updated = false;
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) != 0) {
            continue;
        }
        strscpy(s_snapshots[i].pending_request_type, request_type, sizeof(s_snapshots[i].pending_request_type));
        strscpy(s_snapshots[i].pending_request_id, request_id, sizeof(s_snapshots[i].pending_request_id));
        strscpy(s_snapshots[i].pending_request_prompt, prompt_text ? prompt_text : "",
                sizeof(s_snapshots[i].pending_request_prompt));
        updated = true;
        break;
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return updated;
}

bool turn_context_clear_pending_request(const char *chat_id,
                                        const char *request_type,
                                        const char *request_id)
{
    if (!chat_id || !chat_id[0]) {
        return false;
    }

    bool updated = false;
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) != 0) {
            continue;
        }
        if (request_type && request_type[0] &&
            strcmp(s_snapshots[i].pending_request_type, request_type) != 0) {
            continue;
        }
        if (request_id && request_id[0] &&
            strcmp(s_snapshots[i].pending_request_id, request_id) != 0) {
            continue;
        }
        memset(s_snapshots[i].pending_request_type, 0, sizeof(s_snapshots[i].pending_request_type));
        memset(s_snapshots[i].pending_request_id, 0, sizeof(s_snapshots[i].pending_request_id));
        memset(s_snapshots[i].pending_request_prompt, 0, sizeof(s_snapshots[i].pending_request_prompt));
        updated = true;
        break;
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return updated;
}

bool turn_context_set_delegate_resume_consumed(const char *chat_id,
                                               const char *coordinator_id,
                                               unsigned long visible_revision)
{
    if (!chat_id || !chat_id[0] || !coordinator_id || !coordinator_id[0] || visible_revision == 0) {
        return false;
    }

    bool updated = false;
    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) != 0) {
            continue;
        }
        turn_context_record_consumed_delegate_resume(&s_snapshots[i],
                                                     coordinator_id,
                                                     visible_revision);
        updated = true;
        break;
    }
    if (!updated) {
        struct turn_snapshot snap;
        memset(&snap, 0, sizeof(snap));
        strscpy(snap.chat_id, chat_id, sizeof(snap.chat_id));
        turn_context_record_consumed_delegate_resume(&snap,
                                                     coordinator_id,
                                                     visible_revision);

        if (s_count >= MAX_SNAPSHOTS) {
            cJSON_Delete(s_snapshots[0].messages);
            kfree(s_snapshots[0].system_prompt);
            memmove(&s_snapshots[0], &s_snapshots[1], (MAX_SNAPSHOTS - 1) * sizeof(struct turn_snapshot));
            s_count--;
        }
        s_snapshots[s_count++] = snap;
        updated = true;
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return updated;
}

bool turn_context_has_delegate_resume_consumed(const char *chat_id,
                                               const char *coordinator_id,
                                               unsigned long visible_revision)
{
    bool consumed = false;

    if (!chat_id || !chat_id[0] || !coordinator_id || !coordinator_id[0] || visible_revision == 0) {
        return false;
    }

    pthread_mutex_lock(&s_snapshots_mutex);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_snapshots[i].chat_id, chat_id) != 0) {
            continue;
        }
        consumed = turn_context_has_consumed_delegate_resume_in_snapshot(&s_snapshots[i],
                                                                         coordinator_id,
                                                                         visible_revision);
        break;
    }
    pthread_mutex_unlock(&s_snapshots_mutex);
    return consumed;
}

/* 非阻塞检查执行核是否有回复 */
bool core_has_reply(void)
{
    struct core_task task;
    memset(&task, 0, sizeof(task));
    /* core_recv with 0 timeout = non-blocking peek */
    if (core_recv(CORE_SCHEDULER, &task, 0) == 0) {
        kfree(task.payload);
        kfree(task.result);
        return true;
    }
    return false;
}
