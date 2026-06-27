#include "delegate_turn_directive.h"

#include <pthread.h>
#include <string.h>

#include "linux/kernel.h"

#define MAX_DIRECTIVES 16
#define DIRECTIVE_JSON_MAX 4096

struct delegate_turn_directive_entry {
    char chat_id[64];
    char directive_json[DIRECTIVE_JSON_MAX];
};

static struct delegate_turn_directive_entry s_entries[MAX_DIRECTIVES];
static int s_entry_count = 0;
static pthread_mutex_t s_entries_mutex = PTHREAD_MUTEX_INITIALIZER;

static int find_entry_index(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return -1;
    }

    for (int i = 0; i < s_entry_count; i++) {
        if (strcmp(s_entries[i].chat_id, chat_id) == 0) {
            return i;
        }
    }
    return -1;
}

bool delegate_turn_directive_store(const char *chat_id, const char *directive_json)
{
    if (!chat_id || !chat_id[0] || !directive_json || !directive_json[0]) {
        return false;
    }

    pthread_mutex_lock(&s_entries_mutex);

    int idx = find_entry_index(chat_id);
    if (idx < 0) {
        if (s_entry_count >= MAX_DIRECTIVES) {
            memmove(&s_entries[0], &s_entries[1],
                    sizeof(s_entries[0]) * (MAX_DIRECTIVES - 1));
            s_entry_count = MAX_DIRECTIVES - 1;
        }
        idx = s_entry_count++;
        memset(&s_entries[idx], 0, sizeof(s_entries[idx]));
        strscpy(s_entries[idx].chat_id, chat_id, sizeof(s_entries[idx].chat_id));
    }

    strscpy(s_entries[idx].directive_json,
            directive_json,
            sizeof(s_entries[idx].directive_json));
    pthread_mutex_unlock(&s_entries_mutex);
    return true;
}

bool delegate_turn_directive_load_copy(const char *chat_id, char *buf, size_t buf_size)
{
    if (!chat_id || !chat_id[0] || !buf || buf_size == 0) {
        return false;
    }

    buf[0] = '\0';
    pthread_mutex_lock(&s_entries_mutex);
    int idx = find_entry_index(chat_id);
    if (idx < 0 || !s_entries[idx].directive_json[0]) {
        pthread_mutex_unlock(&s_entries_mutex);
        return false;
    }

    strscpy(buf, s_entries[idx].directive_json, buf_size);
    pthread_mutex_unlock(&s_entries_mutex);
    return true;
}

void delegate_turn_directive_clear(const char *chat_id)
{
    if (!chat_id || !chat_id[0]) {
        return;
    }

    pthread_mutex_lock(&s_entries_mutex);
    int idx = find_entry_index(chat_id);
    if (idx >= 0) {
        if (idx < s_entry_count - 1) {
            memmove(&s_entries[idx], &s_entries[idx + 1],
                    sizeof(s_entries[0]) * (s_entry_count - idx - 1));
        }
        s_entry_count--;
    }
    pthread_mutex_unlock(&s_entries_mutex);
}
