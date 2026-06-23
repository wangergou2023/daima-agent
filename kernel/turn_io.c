/* 单回合同步执行路径的临时资源管理。 */
#include "turn_io.h"

#include "drivers/platform/platform.h"
#include "linux/slab.h"

bool agent_turn_io_init(agent_turn_io_t *io)
{
	if (!io) {
		return false;
	}

	io->system_prompt = platform_calloc(1, AGENT_TURN_IO_BUF_SIZE);
	io->history_json = platform_calloc(1, AGENT_TURN_IO_BUF_SIZE);
	io->messages = NULL;
	if (!io->system_prompt || !io->history_json) {
		kfree(io->system_prompt);
		kfree(io->history_json);
		io->system_prompt = NULL;
		io->history_json = NULL;
		return false;
	}

	return true;
}

void agent_turn_io_cleanup(agent_turn_io_t *io)
{
	if (!io) {
		return;
	}

	cJSON_Delete(io->messages);
	kfree(io->system_prompt);
	kfree(io->history_json);
}
