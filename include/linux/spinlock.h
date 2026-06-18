#pragma once

#include <pthread.h>

struct spinlock {
	pthread_spinlock_t lock;
};

static inline void spin_lock_init(struct spinlock *lock)
{
	pthread_spin_init(&lock->lock, PTHREAD_PROCESS_PRIVATE);
}

static inline void spin_lock(struct spinlock *lock)
{
	pthread_spin_lock(&lock->lock);
}

static inline void spin_unlock(struct spinlock *lock)
{
	pthread_spin_unlock(&lock->lock);
}

static inline void spin_lock_destroy(struct spinlock *lock)
{
	pthread_spin_destroy(&lock->lock);
}
