#include "os.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Queue implementation ───────────────────────────────────── */

struct daima_queue {
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    unsigned char *buffer;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

static int wait_with_timeout(pthread_cond_t *cond, pthread_mutex_t *mutex, uint32_t timeout_ms)
{
    if (timeout_ms == DAIMA_WAIT_FOREVER) {
        return pthread_cond_wait(cond, mutex);
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(cond, mutex, &ts);
}

daima_queue_t *daima_queue_create(size_t queue_length, size_t item_size)
{
    struct daima_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->item_size = item_size;
    q->capacity = queue_length;
    q->buffer = calloc(queue_length, item_size);
    if (!q->buffer) {
        free(q);
        return NULL;
    }
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return q;
}

bool daima_queue_send(daima_queue_t *queue, const void *item, uint32_t timeout_ms)
{
    struct daima_queue *q = (struct daima_queue *)queue;
    if (!q) return false;

    pthread_mutex_lock(&q->mutex);
    while (q->count == q->capacity) {
        if (timeout_ms == 0 ||
            wait_with_timeout(&q->not_full, &q->mutex, timeout_ms) != 0) {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
    }

    memcpy(q->buffer + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

bool daima_queue_receive(daima_queue_t *queue, void *out_item, uint32_t timeout_ms)
{
    struct daima_queue *q = (struct daima_queue *)queue;
    if (!q) return false;

    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        if (timeout_ms == 0 ||
            wait_with_timeout(&q->not_empty, &q->mutex, timeout_ms) != 0) {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
    }

    memcpy(out_item, q->buffer + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

/* ── Event groups ────────────────────────────────────────────── */

struct daima_event_group {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t bits;
};

daima_event_group_t *daima_event_group_create(void)
{
    struct daima_event_group *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    pthread_mutex_init(&g->mutex, NULL);
    pthread_cond_init(&g->cond, NULL);
    g->bits = 0;
    return g;
}

uint32_t daima_event_group_wait_bits(daima_event_group_t *group,
                                    uint32_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    uint32_t timeout_ms)
{
    struct daima_event_group *g = (struct daima_event_group *)group;
    if (!g) return 0;

    pthread_mutex_lock(&g->mutex);

    while (1) {
        uint32_t current = g->bits;
        bool ready = wait_for_all ? ((current & bits_to_wait_for) == bits_to_wait_for)
                                  : ((current & bits_to_wait_for) != 0);
        if (ready) {
            if (clear_on_exit) {
                g->bits &= ~bits_to_wait_for;
            }
            pthread_mutex_unlock(&g->mutex);
            return current;
        }

        if (timeout_ms == 0 ||
            wait_with_timeout(&g->cond, &g->mutex, timeout_ms) != 0) {
            pthread_mutex_unlock(&g->mutex);
            return g->bits;
        }
    }
}

uint32_t daima_event_group_set_bits(daima_event_group_t *group, uint32_t bits)
{
    struct daima_event_group *g = (struct daima_event_group *)group;
    if (!g) return 0;

    pthread_mutex_lock(&g->mutex);
    g->bits |= bits;
    pthread_cond_broadcast(&g->cond);
    uint32_t out = g->bits;
    pthread_mutex_unlock(&g->mutex);
    return out;
}

/* ── Tasks ───────────────────────────────────────────────────── */

struct daima_task {
    pthread_t thread;
};

typedef struct {
    daima_task_fn_t fn;
    void *arg;
} task_wrapper_t;

static void *task_trampoline(void *arg)
{
    task_wrapper_t *wrap = (task_wrapper_t *)arg;
    daima_task_fn_t fn = wrap->fn;
    void *fn_arg = wrap->arg;
    free(wrap);
    fn(fn_arg);
    return NULL;
}

bool daima_task_create(daima_task_fn_t task_func, const char *name,
                      uint32_t stack_size, void *arg,
                      uint32_t priority, daima_task_t **out_handle)
{
    (void)name; (void)stack_size; (void)priority;
    struct daima_task *task = malloc(sizeof(*task));
    if (!task) return false;

    task_wrapper_t *wrap = malloc(sizeof(*wrap));
    if (!wrap) { free(task); return false; }
    wrap->fn = task_func;
    wrap->arg = arg;

    if (pthread_create(&task->thread, NULL, task_trampoline, wrap) != 0) {
        free(wrap);
        free(task);
        return false;
    }

    pthread_detach(task->thread);
    if (out_handle) {
        *out_handle = task;
    } else {
        free(task);
    }
    return true;
}

void daima_task_delay(uint32_t delay_ms)
{
    if (delay_ms == DAIMA_WAIT_FOREVER) {
        while (1) { sleep(1); }
    }
    usleep((useconds_t)delay_ms * 1000U);
}

void daima_task_delete(daima_task_t *handle)
{
    if (!handle) {
        pthread_exit(NULL);
        return;
    }
    struct daima_task *task = (struct daima_task *)handle;
    pthread_cancel(task->thread);
    free(task);
}

/* ── Timers ──────────────────────────────────────────────────── */

struct daima_timer {
    uint32_t period_ms;
    bool auto_reload;
    daima_timer_cb_t callback;
    void *timer_id;
    int running;
    pthread_t thread;
};

static void *timer_thread(void *arg)
{
    struct daima_timer *t = (struct daima_timer *)arg;
    while (t->running) {
        usleep((useconds_t)t->period_ms * 1000U);
        if (!t->running) break;
        t->callback((daima_timer_t *)t);
        if (!t->auto_reload) {
            t->running = 0;
            break;
        }
    }
    return NULL;
}

daima_timer_t *daima_timer_create(const char *name,
                                uint32_t period_ms,
                                bool auto_reload,
                                void *timer_id,
                                daima_timer_cb_t callback)
{
    (void)name;
    struct daima_timer *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->period_ms = period_ms;
    t->auto_reload = auto_reload;
    t->callback = callback;
    t->timer_id = timer_id;
    t->running = 0;
    return (daima_timer_t *)t;
}

bool daima_timer_start(daima_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct daima_timer *t = (struct daima_timer *)timer;
    if (!t) return false;
    if (t->running) return true;
    t->running = 1;
    if (pthread_create(&t->thread, NULL, timer_thread, t) != 0) {
        t->running = 0;
        return false;
    }
    pthread_detach(t->thread);
    return true;
}

bool daima_timer_stop(daima_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct daima_timer *t = (struct daima_timer *)timer;
    if (!t) return false;
    t->running = 0;
    return true;
}

bool daima_timer_delete(daima_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct daima_timer *t = (struct daima_timer *)timer;
    if (!t) return false;
    t->running = 0;
    free(t);
    return true;
}
