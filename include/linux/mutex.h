/* 互斥锁接口：pthread mutex 封装，用于临界区保护。 */

#pragma once

#include <pthread.h>

/* 互斥锁 */
struct mutex {
	pthread_mutex_t lock;  /* 底层 POSIX 互斥锁 */
};

/* 加锁（阻塞直到获取锁） */
#define mutex_lock(m)       pthread_mutex_lock(&(m)->lock)
/* 解锁 */
#define mutex_unlock(m)     pthread_mutex_unlock(&(m)->lock)
/* 初始化互斥锁 */
#define mutex_init(m)       pthread_mutex_init(&(m)->lock, NULL)
/* 销毁互斥锁（释放系统资源） */
#define mutex_destroy(m)    pthread_mutex_destroy(&(m)->lock)
/* 尝试加锁（非阻塞，成功返回 1，失败返回 0） */
#define mutex_trylock(m)    (pthread_mutex_trylock(&(m)->lock) == 0 ? 1 : 0)
