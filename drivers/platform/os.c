#include "os.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "linux/slab.h"

/* ── Queue implementation ───────────────────────────────────── */

struct os_queue {
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
    if (timeout_ms == WAIT_FOREVER) {
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

queue_t *queue_create(size_t queue_length, size_t item_size)
{
    struct os_queue *q = kzalloc(sizeof(*q), GFP_KERNEL);
    if (!q) return NULL;
    q->item_size = item_size;
    q->capacity = queue_length;
    q->buffer = kzalloc(queue_length * item_size, GFP_KERNEL);
    if (!q->buffer) {
        kfree(q);
        return NULL;
    }
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return q;
}

bool queue_send(queue_t *queue, const void *item, uint32_t timeout_ms)
{
    struct os_queue *q = (struct os_queue *)queue;
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

bool queue_receive(queue_t *queue, void *out_item, uint32_t timeout_ms)
{
    struct os_queue *q = (struct os_queue *)queue;
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

/* ── Tasks ───────────────────────────────────────────────────── */

struct os_task {
    pthread_t thread;
};

typedef struct {
    os_task_fn_t fn;
    void *arg;
} task_wrapper_t;

static void *task_trampoline(void *arg)
{
    task_wrapper_t *wrap = (task_wrapper_t *)arg;
    os_task_fn_t fn = wrap->fn;
    void *fn_arg = wrap->arg;
    kfree(wrap);
    fn(fn_arg);
    return NULL;
}

bool task_create(os_task_fn_t task_func, const char *name,
                      uint32_t stack_size, void *arg,
                      uint32_t priority, os_task_t **out_handle)
{
    (void)name; (void)stack_size; (void)priority;
    struct os_task *task = kmalloc(sizeof(*task), GFP_KERNEL);
    if (!task) return false;

    task_wrapper_t *wrap = kmalloc(sizeof(*wrap), GFP_KERNEL);
    if (!wrap) { kfree(task); return false; }
    wrap->fn = task_func;
    wrap->arg = arg;

    if (pthread_create(&task->thread, NULL, task_trampoline, wrap) != 0) {
        kfree(wrap);
        kfree(task);
        return false;
    }

    pthread_detach(task->thread);
    if (out_handle) {
        *out_handle = task;
    } else {
        kfree(task);
    }
    return true;
}

void task_delay(uint32_t delay_ms)
{
    if (delay_ms == WAIT_FOREVER) {
        while (1) { sleep(1); }
    }
    usleep((useconds_t)delay_ms * 1000U);
}

void os_task_delete(os_task_t *handle)
{
    if (!handle) {
        pthread_exit(NULL);
        return;
    }
    struct os_task *task = (struct os_task *)handle;
    pthread_cancel(task->thread);
    kfree(task);
}

/* ── Timers ──────────────────────────────────────────────────── */

struct os_timer {
    uint32_t period_ms;
    bool auto_reload;
    os_timer_cb_t callback;
    void *timer_id;
    int running;
    pthread_t thread;
};

static void *timer_thread(void *arg)
{
    struct os_timer *t = (struct os_timer *)arg;
    while (t->running) {
        usleep((useconds_t)t->period_ms * 1000U);
        if (!t->running) break;
        t->callback((os_timer_t *)t);
        if (!t->auto_reload) {
            t->running = 0;
            break;
        }
    }
    return NULL;
}

os_timer_t *os_timer_create(const char *name,
                                uint32_t period_ms,
                                bool auto_reload,
                                void *timer_id,
                                os_timer_cb_t callback)
{
    (void)name;
    struct os_timer *t = kzalloc(sizeof(*t), GFP_KERNEL);
    if (!t) return NULL;
    t->period_ms = period_ms;
    t->auto_reload = auto_reload;
    t->callback = callback;
    t->timer_id = timer_id;
    t->running = 0;
    return (os_timer_t *)t;
}

bool os_timer_start(os_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct os_timer *t = (struct os_timer *)timer;
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

bool os_timer_stop(os_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct os_timer *t = (struct os_timer *)timer;
    if (!t) return false;
    t->running = 0;
    return true;
}

bool os_timer_delete(os_timer_t *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct os_timer *t = (struct os_timer *)timer;
    if (!t) return false;
    t->running = 0;
    kfree(t);
    return true;
}
