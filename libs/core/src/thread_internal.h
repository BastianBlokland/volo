#pragma once
#include "core_thread.h"

#define thread_pal_stacksize (4 * usize_mebibyte)

#if defined(VOLO_LINUX)
#define thread_pal_rettype void*
#elif defined(VOLO_WIN32)
#define thread_pal_rettype unsigned long
#else
_Static_assert(false, "Unsupported platform");
#endif

i64  thread_pal_pid();
i64  thread_pal_tid();
u16  thread_pal_core_count();
void thread_pal_set_name(String);

i64  thread_pal_atomic_load_i64(i64*);
void thread_pal_atomic_store_i64(i64*, i64 value);
i64  thread_pal_atomic_exchange_i64(i64*, i64 value);
bool thread_pal_atomic_compare_exchange_i64(i64*, i64* expected, i64 value);

ThreadHandle thread_pal_start(thread_pal_rettype (*)(void*), void*);
void         thread_pal_join(ThreadHandle);
void         thread_pal_yield();
void         thread_pal_sleep(TimeDuration);

ThreadMutex thread_pal_mutex_create(Allocator*);
void        thread_pal_mutex_destroy(ThreadMutex);
void        thread_pal_mutex_lock(ThreadMutex);
bool        thread_pal_mutex_trylock(ThreadMutex);
void        thread_pal_mutex_unlock(ThreadMutex);
