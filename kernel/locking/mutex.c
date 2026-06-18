/* 内核互斥锁 —— 对齐 Linux kernel/locking/mutex.c API */
#include "linux/mutex.h"
#include "linux/printk.h"
#include <errno.h>

void mutex_init(struct mutex *lock)
{
	pthread_mutex_init(&lock->lock, NULL);
}

void mutex_destroy(struct mutex *lock)
{
	pthread_mutex_destroy(&lock->lock);
}

void mutex_lock(struct mutex *lock)
{
	int ret = pthread_mutex_lock(&lock->lock);
	if (ret != 0)
		pr_err("mutex_lock failed: %d", ret);
}

int mutex_lock_interruptible(struct mutex *lock)
{
	/* userspace 无信号打断语义，fallback 到普通 lock */
	mutex_lock(lock);
	return 0;
}

int mutex_trylock(struct mutex *lock)
{
	int ret = pthread_mutex_trylock(&lock->lock);
	if (ret == EBUSY)
		return 0;   /* 获取失败 */
	if (ret != 0) {
		pr_err("mutex_trylock failed: %d", ret);
		return 0;
	}
	return 1;        /* 获取成功 */
}

void mutex_unlock(struct mutex *lock)
{
	pthread_mutex_unlock(&lock->lock);
}
