/* Turn messages 组装接口。 */

#pragma once

#include <stddef.h>

#include "bus.h"
#include "cjson.h"
#include "err.h"

err_t agent_turn_build_messages(const struct message *msg,
				char *history_json,
				size_t history_json_size,
				cJSON **out_messages);
