/* Turn interview 短路：显式处理澄清问题。 */

#include "turn_interview.h"

#include <stdlib.h>
#include <string.h>

#include "interview.h"

err_t agent_turn_try_interview(struct message *msg, char **out_final_text)
{
	if (!msg || !out_final_text) {
		return ERR_INVALID_ARG;
	}
	if (msg->intent != INTENT_IMPLEMENT) {
		return ERR_FAIL;
	}

	prometheus_state_t p_state;
	if (prometheus_check_needs_interview(msg->content ? msg->content : "", &p_state) != 0 ||
	    !p_state.needs_interview) {
		return ERR_FAIL;
	}

	*out_final_text = strdup(p_state.questions);
	return *out_final_text ? 0 : ERR_NO_MEM;
}
