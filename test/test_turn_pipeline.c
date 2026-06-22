#include "kernel/turn_pipeline.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int g_replace_calls;
static int g_before_calls;
static int g_run_calls;
static int g_finish_hook_calls;
static int g_finish_calls;
static int g_cancel_begin_calls;
static int g_seen_iteration;
static bool g_seen_budget_exhausted;
static bool g_seen_cancelled;
static char g_seen_finish_response[128];
static const char *g_seen_tools_json;
static const char *g_seen_model_override;
static uint64_t g_cancel_token = 42;
static int g_replace_err = ERR_FAIL;
static int g_before_err;
static int g_run_err;
static int g_run_iteration = 3;
static bool g_run_budget_exhausted;
static bool g_run_cancelled;
static const char *g_replace_text;
static const char *g_run_text;

int printk(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}

uint64_t agent_cancel_begin_turn(const char *chat_id)
{
	assert(chat_id != NULL);
	g_cancel_begin_calls++;
	return g_cancel_token;
}

err_t agent_hooks_trigger_replace_run(struct message *msg,
				      char *system_prompt,
				      cJSON *messages,
				      const char *tools_json,
				      char **out_final_text)
{
	(void)msg;
	(void)system_prompt;
	(void)messages;
	g_replace_calls++;
	g_seen_tools_json = tools_json;
	if (g_replace_err == 0 && g_replace_text) {
		*out_final_text = strdup(g_replace_text);
		assert(*out_final_text != NULL);
	}
	return g_replace_err;
}

err_t agent_hooks_trigger_before_run(struct message *msg,
				     const char **model_override,
				     const char *tools_json)
{
	(void)msg;
	g_before_calls++;
	g_seen_tools_json = tools_json;
	*model_override = "test-model";
	g_seen_model_override = *model_override;
	return g_before_err;
}

err_t agent_turn_run(const char *system_prompt,
		     cJSON *messages,
		     const char *tools_json,
		     const struct message *msg,
		     const char *model_override,
		     uint64_t cancel_token,
		     char **out_final_text,
		     char **out_reasoning_text,
		     int *out_iteration,
		     bool *out_tool_budget_exhausted,
		     bool *out_cancelled)
{
	(void)system_prompt;
	(void)messages;
	(void)msg;
	g_run_calls++;
	assert(cancel_token == g_cancel_token);
	g_seen_tools_json = tools_json;
	g_seen_model_override = model_override;
	if (g_run_text) {
		*out_final_text = strdup(g_run_text);
		assert(*out_final_text != NULL);
	}
	*out_reasoning_text = NULL;
	*out_iteration = g_run_iteration;
	*out_tool_budget_exhausted = g_run_budget_exhausted;
	*out_cancelled = g_run_cancelled;
	return g_run_err;
}

void agent_hooks_trigger_finish(struct message *msg, const char *response)
{
	(void)msg;
	g_finish_hook_calls++;
	if (response) {
		strncpy(g_seen_finish_response, response, sizeof(g_seen_finish_response) - 1);
		g_seen_finish_response[sizeof(g_seen_finish_response) - 1] = '\0';
	} else {
		g_seen_finish_response[0] = '\0';
	}
}

void agent_turn_finish(struct message *msg,
		       char **io_final_text,
		       char **io_reasoning_text,
		       err_t turn_err,
		       int iteration,
		       bool tool_budget_exhausted,
		       bool cancelled)
{
	(void)msg;
	(void)turn_err;
	g_finish_calls++;
	g_seen_iteration = iteration;
	g_seen_budget_exhausted = tool_budget_exhausted;
	g_seen_cancelled = cancelled;
	free(*io_final_text);
	free(*io_reasoning_text);
	*io_final_text = NULL;
	*io_reasoning_text = NULL;
}

static void reset_state(void)
{
	g_replace_calls = 0;
	g_before_calls = 0;
	g_run_calls = 0;
	g_finish_hook_calls = 0;
	g_finish_calls = 0;
	g_cancel_begin_calls = 0;
	g_seen_iteration = -1;
	g_seen_budget_exhausted = false;
	g_seen_cancelled = false;
	g_seen_finish_response[0] = '\0';
	g_seen_tools_json = NULL;
	g_seen_model_override = NULL;
	g_replace_err = ERR_FAIL;
	g_before_err = 0;
	g_run_err = 0;
	g_run_iteration = 3;
	g_run_budget_exhausted = false;
	g_run_cancelled = false;
	g_replace_text = NULL;
	g_run_text = NULL;
}

static void test_replace_run_path_skips_before_and_run(void)
{
	reset_state();
	g_replace_err = 0;
	g_replace_text = "handled by replace";

	struct message msg = {0};
	cJSON *messages = cJSON_CreateArray();
	assert(messages != NULL);

	agent_run_prepared_turn(&msg, "prompt", messages, "[]", "chat-a", 5);

	assert(g_replace_calls == 1);
	assert(g_before_calls == 0);
	assert(g_run_calls == 0);
	assert(g_cancel_begin_calls == 0);
	assert(g_finish_hook_calls == 1);
	assert(g_finish_calls == 1);
	assert(strcmp(g_seen_finish_response, "handled by replace") == 0);
	assert(g_seen_iteration == 5);

	cJSON_Delete(messages);
}

static void test_before_and_run_path_applies_iteration_offset(void)
{
	reset_state();
	g_run_text = "handled by run";
	g_run_iteration = 4;

	struct message msg = {0};
	cJSON *messages = cJSON_CreateArray();
	assert(messages != NULL);

	agent_run_prepared_turn(&msg, "prompt", messages, "[]", "chat-b", 7);

	assert(g_replace_calls == 1);
	assert(g_before_calls == 1);
	assert(g_run_calls == 1);
	assert(g_cancel_begin_calls == 1);
	assert(strcmp(g_seen_tools_json, "[]") == 0);
	assert(strcmp(g_seen_model_override, "test-model") == 0);
	assert(g_finish_hook_calls == 1);
	assert(g_finish_calls == 1);
	assert(strcmp(g_seen_finish_response, "handled by run") == 0);
	assert(g_seen_iteration == 11);

	cJSON_Delete(messages);
}

static void test_finish_receives_budget_and_cancelled_flags_from_run(void)
{
	reset_state();
	g_run_text = "final";
	g_run_budget_exhausted = true;
	g_run_cancelled = true;

	struct message msg = {0};
	cJSON *messages = cJSON_CreateArray();
	assert(messages != NULL);

	agent_run_prepared_turn(&msg, "prompt", messages, "[]", "chat-c", 0);

	assert(g_seen_budget_exhausted == true);
	assert(g_seen_cancelled == true);

	cJSON_Delete(messages);
}

static void test_finalize_turn_applies_iteration_offset_and_finish_hook(void)
{
	reset_state();
	struct message msg = {0};
	char *final_text = strdup("done");
	char *reasoning_text = NULL;

	assert(final_text != NULL);

	agent_finalize_turn(&msg, &final_text, &reasoning_text, ERR_FAIL, 2, true, false, 9);

	assert(g_finish_hook_calls == 1);
	assert(g_finish_calls == 1);
	assert(strcmp(g_seen_finish_response, "done") == 0);
	assert(g_seen_iteration == 11);
	assert(g_seen_budget_exhausted == true);
	assert(g_seen_cancelled == false);
	assert(final_text == NULL);
}

int main(void)
{
	test_replace_run_path_skips_before_and_run();
	test_before_and_run_path_applies_iteration_offset();
	test_finish_receives_budget_and_cancelled_flags_from_run();
	test_finalize_turn_applies_iteration_offset_and_finish_hook();
	return 0;
}
