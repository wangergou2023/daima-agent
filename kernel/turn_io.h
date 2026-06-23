/* 单回合 prepare/run 阶段的临时资源容器。 */

#pragma once

#include <stdbool.h>

#include "cjson.h"

#define AGENT_TURN_IO_BUF_SIZE 131072

typedef struct {
	char *system_prompt;
	char *history_json;
	cJSON *messages;
} agent_turn_io_t;

bool agent_turn_io_init(agent_turn_io_t *io);
void agent_turn_io_cleanup(agent_turn_io_t *io);
