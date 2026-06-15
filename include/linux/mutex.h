#pragma once

#include <pthread.h>

struct mutex {
    pthread_mutex_t lock;
};

#define mutex_lock(m)   pthread_mutex_lock(&(m)->lock)
#define mutex_unlock(m) pthread_mutex_unlock(&(m)->lock)
#define mutex_init(m)   pthread_mutex_init(&(m)->lock, NULL)
