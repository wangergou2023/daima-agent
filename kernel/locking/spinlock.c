/* 内核自旋锁 —— 对齐 Linux kernel/locking/spinlock.h API */

#include "linux/spinlock.h"
#include "linux/printk.h"

void spin_lock_init(struct spinlock *lock)
{
	pthread_spin_init(&lock->lock, PTHREAD_PROCESS_PRIVATE);
}

void spin_lock(struct spinlock *lock)
{
	pthread_spin_lock(&lock->lock);
}

void spin_lock_irqsave(struct spinlock *lock, unsigned long *flags)
{
	(void)flags;
	pthread_spin_lock(&lock->lock);
}

void spin_unlock(struct spinlock *lock)
{
	pthread_spin_unlock(&lock->lock);
}

void spin_unlock_irqrestore(struct spinlock *lock, unsigned long *flags)
{
	(void)flags;
	pthread_spin_unlock(&lock->lock);
}

void spin_lock_destroy(struct spinlock *lock)
{
	pthread_spin_destroy(&lock->lock);
}
