#include "thrdpool.h"
#include "msgqueue.h"
#include <complex.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct __thrdpool
{
    msgqueue_t* msgqueue;
    size_t nthreads;
    size_t stacksize;
    pthread_t tid;
    pthread_mutex_t mutex;
    pthread_key_t key;
    pthread_cond_t* terminate;
};

struct __thrdpool_task_entry
{
    void* link;
    struct thrdpool_task task;
};

static pthread_t __zero_tid;

static void* __thrdpool_routine(void* arg)
{
    thrdpool_t* pool = (thrdpool_t*)arg;
    struct __thrdpool_task_entry* entry;
    void (*task_routine)(void*);
    void* task_context;
    pthread_t tid;

    pthread_setspecific(pool->key, pool);
    while (!pool->terminate)
    {
        entry = (struct __thrdpool_task_entry*)msgqueue_get(pool->msgqueue);
        if (!entry)
            break;

        task_routine = entry->task.routine;
        task_context = entry->task.context;

        free(entry);
        task_routine(task_context);

        if (pool->nthreads == 0)
        {
            free(pool);
            return NULL;
        }
    }

    pthread_mutex_lock(&pool->mutex);
    tid = pool->tid;

    pool->tid = pthread_self();
    if (--pool->nthreads == 0)
        pthread_cond_signal(pool->terminate);

    pthread_mutex_unlock(&pool->mutex);

    if (memcmp(&tid, &__zero_tid, sizeof(pthread_t)) != 0)
        pthread_join(tid, NULL);

    return NULL;
}

static void __thrdpool_terminate(int in_pool, thrdpool_t* pool)
{
    pthread_cond_t term = PTHREAD_COND_INITIALIZER;

    pthread_mutex_lock(&pool->mutex);
    msgqueue_set_nonblock(pool->msgqueue);
    pool->terminate = &term;

    if (in_pool)
    {
        pthread_detach(pthread_self());
        pool->nthreads--;
    }

    while (pool->nthreads > 0)
    {
        pthread_cond_wait(&term, &pool->mutex);
    }

    pthread_mutex_unlock(&pool->mutex);
    if (memcpy(&pool->tid, &__zero_tid, sizeof(pthread_t)) != 0)
    {
        pthread_join(pool->tid, NULL);
    }
}

static int __thrdpool_create_threads(size_t nthreads, thrdpool_t* pool)
{
    pthread_attr_t attr;
    pthread_t tid;
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret == 0)
    {
        if (pool->stacksize)
        {
            pthread_attr_setstacksize(&attr, pool->stacksize);
        }
        while (pool->nthreads < nthreads)
        {
            ret = pthread_create(&tid, &attr, __thrdpool_routine, pool);
            if (ret == 0)
            {
                pool->nthreads++;
            }
            else
            {
                break;
            }
        }
        pthread_attr_destroy(&attr);

        if (pool->nthreads == nthreads)
            return 0;

        __thrdpool_terminate(0, pool);
    }

    errno = ret;
    return -1;
}

thrdpool_t* thrdpool_create(size_t nthreads, size_t stacksize)
{
    thrdpool_t* pool;
    int ret;

    pool = (thrdpool_t*)malloc(sizeof(thrdpool_t));
    if (!pool)
    {
        return NULL;
    }
    pool->msgqueue = msgqueue_create(0, 0);
    if (pool->msgqueue)
    {
        ret = pthread_mutex_init(&pool->mutex, NULL);
        if (ret == 0)
        {
            // thread local value
            ret = pthread_key_create(&pool->key, NULL);
            if (ret == 0)
            {
                pool->stacksize = stacksize;
                pool->nthreads = 0;
                memset(&pool->tid, 0, sizeof(pthread_t));
                pool->terminate = NULL;
                if (__thrdpool_create_threads(nthreads, pool) >= 0)
                {
                    return pool;
                }
                pthread_key_delete(pool->key);
            }
            pthread_mutex_destroy(&pool->mutex);
        }

        errno = ret;
        msgqueue_destory(pool->msgqueue);
    }

    free(pool);
    return NULL;
}

inline void __thrdpool_schedule(
    const struct thrdpool_task* task, void* buf, thrdpool_t* pool);

void __thrdpool_schedule(
    const struct thrdpool_task* task, void* buf, thrdpool_t* pool)
{
    ((struct __thrdpool_task_entry*)buf)->task = *task;
    msgqueue_put(buf, pool->msgqueue);
}

int thrdpool_schedule(const struct thrdpool_task* task, thrdpool_t* pool)
{
    void* buf = malloc(sizeof(struct __thrdpool_task_entry));
    if (buf)
    {
        __thrdpool_schedule(task, buf, pool);
        return 0;
    }
    return -1;
}

int thrdpool_increase(thrdpool_t* pool)
{
    pthread_attr_t attr;
    pthread_t tid;
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret == 0)
    {
        if (pool->stacksize)
        {
            pthread_attr_setstacksize(&attr, pool->stacksize);
        }

        pthread_mutex_lock(&pool->mutex);

        ret = pthread_create(&tid, &attr, __thrdpool_routine, pool);

        if (ret == 0)
        {
            pool->nthreads++;
        }

        pthread_mutex_unlock(&pool->mutex);
        pthread_attr_destroy(&attr);
        if (ret == 0)
            return 0;
    }

    errno = ret;
    return -1;
}

int thrdpool_in_pool(thrdpool_t* pool)
{
    return pthread_getspecific(pool->key) == pool;
}

void thrdpool_destory(
    void (*pending)(const struct thrdpool_task*), thrdpool_t* pool)
{
    int in_pool = thrdpool_in_pool(pool);

    struct __thrdpool_task_entry* entry;

    __thrdpool_terminate(in_pool, pool);

    while (1)
    {
        entry = (struct __thrdpool_task_entry*)msgqueue_get(pool->msgqueue);
        if (!entry)
            break;
        if (pending)
        {
            pending(&entry->task);
        }
        free(entry);
    }

    pthread_key_delete(pool->key);
    pthread_mutex_destroy(&pool->mutex);
    msgqueue_destory(pool->msgqueue);
    if (!in_pool)
    {
        free(pool);
    }
}