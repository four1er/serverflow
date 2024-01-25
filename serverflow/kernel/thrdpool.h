#ifndef _THREAD_POOL_
#define _THREAD_POOL_

#include <stddef.h>

typedef struct __thrdpool thrdpool_t;

struct thrdpool_task {
    void (*routine)(void *);
    void *context;
};

#ifdef __cplusplus
extern "C" {
#endif

thrdpool_t *thrdpool_create(size_t nthreads, size_t stacksize);
int thrdpool_schedule(const struct thrdpool_task *task, thrdpool_t *pool);
int thrdpool_increase(thrdpool_t *pool);
int thrdpool_in_pool(thrdpool_t *pool);
void thrdpool_destory(
    void (*pending)(const struct thrdpool_task *), thrdpool_t *pool);

#ifdef __cplusplus
}
#endif

#endif