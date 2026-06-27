/* Turn interview 短路：显式处理澄清问题。 */

#include "turn_interview.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "interview.h"
#include "interactive.h"
#include "turn_common.h"
#include "turn_context.h"
#include "drivers/tool/tool_delegate_path_resolve.h"
#include "drivers/tool/tool_delegate_repo_batch.h"
#include "drivers/tool/tool_delegate_types.h"
#include "bus.h"
#include "intent.h"
#include "delegate/delegate_turn_directive.h"
#include "cjson.h"
#include "linux/kernel.h"
#include "linux/printk.h"

static bool interview_channel_supported(const struct message *msg)
{
	return msg && strcmp(msg->channel, CHAN_WEBSOCKET) == 0;
}

static void ensure_parent_interview_snapshot(const struct message *msg)
{
	struct turn_snapshot snap;

	if (!msg || !msg->chat_id[0]) {
		return;
	}
	if (strncmp(msg->chat_id, "delegate_sync_", 14) == 0) {
		return;
	}
	if (turn_context_load_copy(msg->chat_id, &snap)) {
		turn_context_snapshot_cleanup(&snap);
		return;
	}

	memset(&snap, 0, sizeof(snap));
	snprintf(snap.chat_id, sizeof(snap.chat_id), "%s", msg->chat_id);
	snprintf(snap.channel, sizeof(snap.channel), "%s", msg->channel);
	snprintf(snap.source, sizeof(snap.source), "%s", agent_msg_source_or_default(msg));
	turn_context_save(&snap);
}

static err_t append_interview_answer_to_message(struct message *msg,
						const char *questions,
						const char *answer)
{
	if (!msg || !questions || !questions[0] || !answer || !answer[0]) {
		return ERR_INVALID_ARG;
	}

	size_t old_len = msg->content ? strlen(msg->content) : 0;
	size_t q_len = strlen(questions);
	size_t a_len = strlen(answer);
	size_t need = old_len + q_len + a_len + 96;
	char *next = calloc(1, need);
	if (!next) {
		return ERR_NO_MEM;
	}

	snprintf(next,
		 need,
		 "%s\n\n[Interview clarification asked]\n%s\n\n[Interview clarification answer]\n%s",
		 msg->content ? msg->content : "",
		 questions,
		 answer);

	free(msg->content);
	msg->content = next;
	return 0;
}

static bool answer_requests_structured_delegate_batch(const char *answer)
{
	if (!answer || !answer[0]) {
		return false;
	}

	if (strstr(answer, "delegate_task") == NULL &&
	    strstr(answer, "批量委托") == NULL) {
		return false;
	}

	if (strstr(answer, "tasks") == NULL &&
	    strstr(answer, "tasks 数组") == NULL &&
	    strstr(answer, "子任务") == NULL) {
		return false;
	}

	if (strstr(answer, "preflight_tool") != NULL ||
	    strstr(answer, "sudo") != NULL ||
	    strstr(answer, "权限链路") != NULL ||
	    strstr(answer, "第 3 个子任务") != NULL ||
	    strstr(answer, "3 个子任务") != NULL) {
		return true;
	}

	return false;
}

static char *build_structured_delegate_batch_directive(const struct message *msg)
{
	delegate_request_t req;
	char repo_root[512];

	if (!msg || !msg->content) {
		return NULL;
	}
	memset(&req, 0, sizeof(req));
	if (!tool_delegate_extract_single_absolute_repo_path(msg->content, repo_root, sizeof(repo_root)) ||
	    !repo_root[0]) {
		return NULL;
	}
	strscpy(req.target_path, repo_root, sizeof(req.target_path));
	strscpy(req.description, "repo overview", sizeof(req.description));
	strscpy(req.prompt, msg->content, sizeof(req.prompt));
	return tool_delegate_build_repo_root_overview_batch_json(&req, true);
}

static void maybe_store_delegate_turn_directive(const struct message *msg,
						const char *answer)
{
	if (!msg || !msg->chat_id[0]) {
		return;
	}
	if (!answer_requests_structured_delegate_batch(answer)) {
		pr_info("InterviewGate directive skipped: chat=%s answer=%.200s",
			msg->chat_id,
			answer ? answer : "");
		return;
	}

	char *directive = build_structured_delegate_batch_directive(msg);
	if (!directive) {
		return;
	}

	if (delegate_turn_directive_store(msg->chat_id, directive)) {
		pr_info("InterviewGate stored structured delegate directive for chat=%s", msg->chat_id);
	}
	free(directive);
}

err_t agent_turn_append_interview_answer_for_test(struct message *msg,
						  const char *questions,
						  const char *answer)
{
	return append_interview_answer_to_message(msg, questions, answer);
}

err_t agent_turn_apply_interview_answer_for_test(struct message *msg,
						 const char *questions,
						 const char *answer,
						 agent_turn_interview_result_t *out_result)
{
	if (!out_result) {
		return ERR_INVALID_ARG;
	}
	memset(out_result, 0, sizeof(*out_result));
	maybe_store_delegate_turn_directive(msg, answer);
	err_t err = append_interview_answer_to_message(msg, questions, answer);
	if (err == 0) {
		out_result->handled = true;
		out_result->continue_turn = true;
	}
	return err;
}

err_t agent_turn_try_interview(struct message *msg,
			       char **out_final_text,
			       agent_turn_interview_result_t *out_result)
{
	if (!msg || !out_final_text || !out_result) {
		return ERR_INVALID_ARG;
	}
	memset(out_result, 0, sizeof(*out_result));
	pr_info("InterviewGate enter: chat=%s channel=%s source=%s intent=%s content=%.160s",
		msg->chat_id,
		msg->channel,
		msg->source,
		intent_name(msg->intent),
		msg->content ? msg->content : "");
	if (msg->intent != INTENT_IMPLEMENT) {
		pr_info("InterviewGate skip: chat=%s reason=intent_not_implement", msg->chat_id);
		return ERR_FAIL;
	}

	prometheus_state_t p_state;
	if (prometheus_check_needs_interview(msg->content ? msg->content : "", &p_state) != 0 ||
	    !p_state.needs_interview) {
		pr_info("InterviewGate skip: chat=%s reason=needs_interview_false questions=%.200s",
			msg->chat_id,
			p_state.questions);
		return ERR_FAIL;
	}
	pr_info("InterviewGate trigger: chat=%s channel=%s questions=%.200s",
		msg->chat_id,
		msg->channel,
		p_state.questions);

	if (interview_channel_supported(msg)) {
		char request_id[64];
		interactive_request_meta_t meta = {0};
		interactive_reply_t reply = {0};

		snprintf(request_id, sizeof(request_id), "question_%ld_%d",
			 (long)time(NULL), (int)getpid());
		meta.request_type = "question_text";
		meta.request_id = request_id;
		meta.prompt_text = p_state.questions;
		ensure_parent_interview_snapshot(msg);
		err_t request_err = channel_runtime_request_interactive(msg, &meta);
		if (request_err == 0) {
			turn_context_set_pending_request(msg->chat_id,
							 meta.request_type,
							 meta.request_id,
							 meta.prompt_text);
		}
		bool got_reply = false;
		if (request_err == 0) {
			got_reply = channel_runtime_wait_interactive_reply(msg, "question_text", request_id, &reply);
			turn_context_clear_pending_request(msg->chat_id, "question_text", request_id);
			turn_context_remove(msg->chat_id);
		} else {
			turn_context_remove(msg->chat_id);
		}
		pr_info("InterviewGate websocket: chat=%s request_err=%s got_reply=%d cancelled=%d answer=%.160s",
			msg->chat_id,
			err_name(request_err),
			got_reply ? 1 : 0,
			reply.cancelled ? 1 : 0,
			reply.value);
		if (request_err == 0 &&
		    got_reply &&
		    !reply.cancelled && reply.value[0]) {
			maybe_store_delegate_turn_directive(msg, reply.value);
			err_t append_err = append_interview_answer_to_message(msg, p_state.questions, reply.value);
			if (append_err == 0) {
				out_result->handled = true;
				out_result->continue_turn = true;
			}
			return append_err;
		}
	}

	*out_final_text = strdup(p_state.questions);
	pr_info("InterviewGate return_questions: chat=%s final=%.200s",
		msg->chat_id,
		*out_final_text ? *out_final_text : "");
	if (*out_final_text) {
		out_result->handled = true;
		out_result->continue_turn = false;
	}
	return *out_final_text ? 0 : ERR_NO_MEM;
}
