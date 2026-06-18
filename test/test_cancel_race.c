#include "test_utils.h"
#include "cancel.h"
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>

int printk(const char *fmt, ...) { (void)fmt; return 0; }

static volatile int cancel_detected;
static volatile int cancel_begin_done;

static void *turn_thread(void *arg)
{
	const char *chat_id = (const char *)arg;
	uint64_t token = agent_cancel_begin_turn(chat_id);
	cancel_begin_done = 1;

	for (int i = 0; i < 500; i++) {
		if (agent_cancel_is_cancelled(chat_id, token)) {
			cancel_detected = 1;
			break;
		}
		usleep(1000);
	}
	return NULL;
}

int main(void)
{
	test_begin();

	TEST_CASE("cancel after begin_turn detected");
	cancel_detected = 0;
	cancel_begin_done = 0;
	pthread_t t;
	pthread_create(&t, NULL, turn_thread, "chat1");
	while (!cancel_begin_done) usleep(1000);
	agent_cancel_request("chat1", "test");
	pthread_join(t, NULL);
	TEST_ASSERT(cancel_detected == 1, "cancel after begin should be detected");
	TEST_DONE();

	TEST_CASE("no cancel without request");
	cancel_detected = 0;
	cancel_begin_done = 0;
	pthread_create(&t, NULL, turn_thread, "chat2");
	pthread_join(t, NULL);
	TEST_ASSERT(cancel_detected == 0, "no cancel should not trigger");
	TEST_DONE();

	TEST_CASE("begin_turn clears pre-existing cancel flag");
	cancel_detected = 0;
	cancel_begin_done = 0;
	agent_cancel_request("chat3", "early cancel");
	pthread_create(&t, NULL, turn_thread, "chat3");
	pthread_join(t, NULL);
	TEST_ASSERT(cancel_detected == 0, "begin_turn clears cancelled flag (cancel consumed)");
	TEST_DONE();

	TEST_CASE("enter/leave current turn TLS");
	const char *cid = "chat_tls";
	uint64_t tk = 42;
	agent_cancel_enter_current_turn(cid, tk);
	TEST_ASSERT(agent_cancel_current_thread_cancelled() == false, "not cancelled yet");
	agent_cancel_request(cid, "tls test");
	TEST_ASSERT(agent_cancel_current_thread_cancelled() == true, "cancelled via TLS");
	agent_cancel_leave_current_turn();
	TEST_DONE();

	test_summary();
	return _test_failed ? 1 : 0;
}
