/* 内核互斥锁 —— 对齐 Linux kernel/locking/mutex.c API */
#include "linux/mutex.h"
#include "linux/printk.h"
#include <errno.h>

/** 初始化互斥锁（pthread 封装）。 */
void mutex_init(struct mutex *lock)
{
	pthread_mutex_init(&lock->lock, NULL);
}

/** 销毁互斥锁。 */
void mutex_destroy(struct mutex *lock)
{
	pthread_mutex_destroy(&lock->lock);
}

/** 获取互斥锁（阻塞）。 */
void mutex_lock(struct mutex *lock)
{
	int ret = pthread_mutex_lock(&lock->lock);
	if (ret != 0)
		pr_err("mutex_lock failed: %d", ret);
}

/** 可中断获取互斥锁——userspace 无信号打断语义，fallback 到普通 lock。 */
int mutex_lock_interruptible(struct mutex *lock)
{
	/* userspace 无信号打断语义，fallback 到普通 lock */
	mutex_lock(lock);
	return 0;
}

/** 尝试获取互斥锁（非阻塞）。返回 0=失败，1=成功。 */
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

/** 释放互斥锁。 */
void mutex_unlock(struct mutex *lock)
{
	pthread_mutex_unlock(&lock->lock);
}
