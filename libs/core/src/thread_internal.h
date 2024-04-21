#pragma once
#include "core_thread.h"

#define thread_pal_stacksize (usize_mebibyte * 2)

#if defined(VOLO_LINUX)
#define thread_pal_rettype void*
#elif defined(VOLO_WIN32)
#define thread_pal_rettype unsigned long
#else
ASSERT(false, "Unsupported platform");
#endif

void thread_pal_init(void);
void thread_pal_init_late(void);
void thread_pal_teardown(void);

ThreadId thread_pal_pid(void);
ThreadId thread_pal_tid(void);
u16      thread_pal_core_count(void);
void     thread_pal_set_name(String);
bool     thread_pal_set_priority(ThreadPriority);

ThreadHandle thread_pal_start(thread_pal_rettype(SYS_DECL*)(void*), void*);
void         thread_pal_join(ThreadHandle);
void         thread_pal_yield(void);
void         thread_pal_sleep(TimeDuration);
bool         thread_pal_exists(ThreadId);

ThreadMutex thread_pal_mutex_create(Allocator*);
void        thread_pal_mutex_destroy(ThreadMutex);
void        thread_pal_mutex_lock(ThreadMutex);
bool        thread_pal_mutex_trylock(ThreadMutex);
void        thread_pal_mutex_unlock(ThreadMutex);

ThreadCondition thread_pal_cond_create(Allocator*);
void            thread_pal_cond_destroy(ThreadCondition);
void            thread_pal_cond_wait(ThreadCondition, ThreadMutex);
void            thread_pal_cond_signal(ThreadCondition);
void            thread_pal_cond_broadcast(ThreadCondition);
