#pragma once

#include <pthread.h>

struct mutex {
    pthread_mutex_t lock;
};

#define mutex_lock(m)       pthread_mutex_lock(&(m)->lock)
#define mutex_unlock(m)     pthread_mutex_unlock(&(m)->lock)
#define mutex_init(m)       pthread_mutex_init(&(m)->lock, NULL)
#define mutex_destroy(m)    pthread_mutex_destroy(&(m)->lock)
#define mutex_trylock(m)    (pthread_mutex_trylock(&(m)->lock) == 0 ? 1 : 0)
