#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAIMA_WAIT_FOREVER UINT32_MAX

typedef struct daima_queue daima_queue_t;
typedef struct daima_event_group daima_event_group_t;
typedef struct daima_task daima_task_t;
typedef struct daima_timer daima_timer_t;

/* Queues */
daima_queue_t *daima_queue_create(size_t queue_length, size_t item_size);
bool daima_queue_send(daima_queue_t *queue, const void *item, uint32_t timeout_ms);
bool daima_queue_receive(daima_queue_t *queue, void *out_item, uint32_t timeout_ms);

/* Event groups (currently unused but provided for parity) */
daima_event_group_t *daima_event_group_create(void);
uint32_t daima_event_group_wait_bits(daima_event_group_t *group,
                                    uint32_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    uint32_t timeout_ms);
uint32_t daima_event_group_set_bits(daima_event_group_t *group, uint32_t bits);

/* Tasks */
typedef void (*daima_task_fn_t)(void *);
bool daima_task_create(daima_task_fn_t task_func, const char *name,
                      uint32_t stack_size, void *arg,
                      uint32_t priority, daima_task_t **out_handle);
void daima_task_delay(uint32_t delay_ms);
void daima_task_delete(daima_task_t *handle);

/* Timers */
typedef void (*daima_timer_cb_t)(daima_timer_t *timer);
daima_timer_t *daima_timer_create(const char *name,
                                uint32_t period_ms,
                                bool auto_reload,
                                void *timer_id,
                                daima_timer_cb_t callback);
bool daima_timer_start(daima_timer_t *timer, uint32_t timeout_ms);
bool daima_timer_stop(daima_timer_t *timer, uint32_t timeout_ms);
bool daima_timer_delete(daima_timer_t *timer, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
