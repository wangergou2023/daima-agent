/* Turn interview 短路接口。 */

#pragma once

#include "bus.h"
#include "err.h"

typedef struct agent_turn_interview_result {
	bool handled;
	bool continue_turn;
} agent_turn_interview_result_t;

err_t agent_turn_try_interview(struct message *msg,
			       char **out_final_text,
			       agent_turn_interview_result_t *out_result);
err_t agent_turn_append_interview_answer_for_test(struct message *msg,
						  const char *questions,
						  const char *answer);
err_t agent_turn_apply_interview_answer_for_test(struct message *msg,
						 const char *questions,
						 const char *answer,
						 agent_turn_interview_result_t *out_result);
