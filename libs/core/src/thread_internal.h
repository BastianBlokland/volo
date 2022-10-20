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

void thread_pal_init();
void thread_pal_teardown();

i64  thread_pal_pid();
i64  thread_pal_tid();
u16  thread_pal_core_count();
void thread_pal_set_name(String);

i32 thread_pal_atomic_load_i32(i32*);
i64 thread_pal_atomic_load_i64(i64*);

void thread_pal_atomic_store_i32(i32*, i32 value);
void thread_pal_atomic_store_i64(i64*, i64 value);

i32 thread_pal_atomic_exchange_i32(i32*, i32 value);
i64 thread_pal_atomic_exchange_i64(i64*, i64 value);

bool thread_pal_atomic_compare_exchange_i32(i32*, i32* expected, i32 value);
bool thread_pal_atomic_compare_exchange_i64(i64*, i64* expected, i64 value);

i32 thread_pal_atomic_add_i32(i32*, i32 value);
i64 thread_pal_atomic_add_i64(i64*, i64 value);

i32 thread_pal_atomic_sub_i32(i32*, i32 value);
i64 thread_pal_atomic_sub_i64(i64*, i64 value);

ThreadHandle thread_pal_start(thread_pal_rettype (*)(void*), void*);
void         thread_pal_join(ThreadHandle);
void         thread_pal_yield();
void         thread_pal_sleep(TimeDuration);

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
