#include "test_utils.h"
#include "linux/mutex.h"
#include "linux/spinlock.h"
#include <pthread.h>
#include <stdarg.h>

int printk(const char *fmt, ...) { (void)fmt; return 0; }

static int shared_counter;
static struct mutex counter_mutex;
static struct spinlock counter_spin;

static void *mutex_worker(void *arg)
{
	int n = *(int *)arg;
	for (int i = 0; i < n; i++) {
		mutex_lock(&counter_mutex);
		shared_counter++;
		mutex_unlock(&counter_mutex);
	}
	return NULL;
}

static void *spinlock_worker(void *arg)
{
	int n = *(int *)arg;
	for (int i = 0; i < n; i++) {
		spin_lock(&counter_spin);
		shared_counter++;
		spin_unlock(&counter_spin);
	}
	return NULL;
}

int main(void)
{
	test_begin();

	TEST_CASE("mutex basic lock/unlock");
	mutex_init(&counter_mutex);
	mutex_lock(&counter_mutex);
	TEST_ASSERT(1, "lock acquired");
	mutex_unlock(&counter_mutex);
	TEST_ASSERT(1, "unlock succeeded");
	mutex_destroy(&counter_mutex);
	TEST_DONE();

	TEST_CASE("mutex trylock");
	mutex_init(&counter_mutex);
	TEST_ASSERT(mutex_trylock(&counter_mutex) == 1, "trylock succeeds");
	TEST_ASSERT(mutex_trylock(&counter_mutex) == 0, "second trylock fails");
	mutex_unlock(&counter_mutex);
	mutex_destroy(&counter_mutex);
	TEST_DONE();

	TEST_CASE("mutex concurrency");
	mutex_init(&counter_mutex);
	shared_counter = 0;
	int n = 10000;
	pthread_t t1, t2;
	pthread_create(&t1, NULL, mutex_worker, &n);
	pthread_create(&t2, NULL, mutex_worker, &n);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	TEST_ASSERT(shared_counter == 20000, "mutex protects counter");
	mutex_destroy(&counter_mutex);
	TEST_DONE();

	TEST_CASE("spinlock concurrency");
	spin_lock_init(&counter_spin);
	shared_counter = 0;
	pthread_create(&t1, NULL, spinlock_worker, &n);
	pthread_create(&t2, NULL, spinlock_worker, &n);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	TEST_ASSERT(shared_counter == 20000, "spinlock protects counter");
	spin_lock_destroy(&counter_spin);
	TEST_DONE();

	test_summary();
	return _test_failed ? 1 : 0;
}
