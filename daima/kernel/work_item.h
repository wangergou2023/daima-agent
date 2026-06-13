/* Work item JSONL store. */

#pragma once

#include "cJSON.h"
#include "err.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    cJSON *items;
    int invalid_lines;
} work_item_list_t;

bool work_item_type_valid(const char *value);
bool work_item_source_valid(const char *value);
bool work_item_status_valid(const char *value);
bool work_item_priority_valid(const char *value);

void work_item_list_free(work_item_list_t *list);
daima_err_t work_item_store_load(work_item_list_t *out);
daima_err_t work_item_store_add(const cJSON *input, cJSON **out_item);
daima_err_t work_item_store_update(const char *id, const cJSON *input, cJSON **out_item);
daima_err_t work_item_store_batch_update(const cJSON *ids, const char *status, int *out_count);
daima_err_t work_item_store_collect(const char *type, const char *source, const char *title, const char *description);
daima_err_t work_item_store_collect_structured(const cJSON *input, cJSON **out_item);
