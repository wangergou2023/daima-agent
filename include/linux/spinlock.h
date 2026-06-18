/* 自旋锁接口：pthread spinlock 封装，用于短期临界区保护。 */

#pragma once

#include <pthread.h>

/* 自旋锁 */
struct spinlock {
	pthread_spinlock_t lock;  /* 底层 POSIX 自旋锁 */
};

/**
 * 初始化自旋锁。
 * @param lock  自旋锁指针
 */
static inline void spin_lock_init(struct spinlock *lock)
{
	pthread_spin_init(&lock->lock, PTHREAD_PROCESS_PRIVATE);
}

/**
 * 获取自旋锁（忙等待）。
 * @param lock  自旋锁指针
 */
static inline void spin_lock(struct spinlock *lock)
{
	pthread_spin_lock(&lock->lock);
}

/**
 * 释放自旋锁。
 * @param lock  自旋锁指针
 */
static inline void spin_unlock(struct spinlock *lock)
{
	pthread_spin_unlock(&lock->lock);
}

/**
 * 销毁自旋锁（释放系统资源）。
 * @param lock  自旋锁指针
 */
static inline void spin_lock_destroy(struct spinlock *lock)
{
	pthread_spin_destroy(&lock->lock);
}
