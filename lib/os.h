#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAIT_FOREVER UINT32_MAX

typedef struct os_queue queue_t;
typedef struct os_task os_task_t;
typedef struct os_timer os_timer_t;

/* Queues */
queue_t *queue_create(size_t queue_length, size_t item_size);
bool queue_send(queue_t *queue, const void *item, uint32_t timeout_ms);
bool queue_receive(queue_t *queue, void *out_item, uint32_t timeout_ms);

/* Tasks */
typedef void (*os_task_fn_t)(void *);
bool task_create(os_task_fn_t task_func, const char *name,
                      uint32_t stack_size, void *arg,
                      uint32_t priority, os_task_t **out_handle);
void task_delay(uint32_t delay_ms);
void os_task_delete(os_task_t *handle);

/* Timers */
typedef void (*os_timer_cb_t)(os_timer_t *timer);
os_timer_t *os_timer_create(const char *name,
                                uint32_t period_ms,
                                bool auto_reload,
                                void *timer_id,
                                os_timer_cb_t callback);
bool os_timer_start(os_timer_t *timer, uint32_t timeout_ms);
bool os_timer_stop(os_timer_t *timer, uint32_t timeout_ms);
bool os_timer_delete(os_timer_t *timer, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
