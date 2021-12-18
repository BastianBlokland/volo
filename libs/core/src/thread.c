#include "core_alloc.h"
#include "core_thread.h"

#include "init_internal.h"
#include "thread_internal.h"

#include <immintrin.h>

typedef struct {
  String        threadName;
  ThreadRoutine userRoutine;
  void*         userData;
} ThreadRunData;

static thread_pal_rettype thread_runner(void* data) {
  ThreadRunData* runData = (ThreadRunData*)data;

  // Initialize the core library for this thread.
  core_init();

  // Initialize the thread name.
  g_thread_name = runData->threadName;
  thread_pal_set_name(g_thread_name);

  // Invoke the user routine.
  runData->userRoutine(runData->userData);

  // Tear-down the core library for this thread.
  core_teardown();

  // Cleanup thread data.
  string_free(g_alloc_heap, runData->threadName);
  alloc_free_t(g_alloc_heap, runData);

  return null;
}

i64                 g_thread_pid;
i64                 g_thread_main_tid;
THREAD_LOCAL i64    g_thread_tid;
THREAD_LOCAL String g_thread_name;
u16                 g_thread_core_count;

void thread_init() {
  g_thread_pid        = thread_pal_pid();
  g_thread_main_tid   = thread_pal_tid();
  g_thread_name       = string_lit("volo_main");
  g_thread_core_count = thread_pal_core_count();
  thread_pal_set_name(g_thread_name);
}

void thread_init_thread() { g_thread_tid = thread_pal_tid(); }

i64 thread_atomic_load_i64(i64* ptr) { return thread_pal_atomic_load_i64(ptr); }

void thread_atomic_store_i64(i64* ptr, i64 value) { thread_pal_atomic_store_i64(ptr, value); }

i64 thread_atomic_exchange_i64(i64* ptr, i64 value) {
  return thread_pal_atomic_exchange_i64(ptr, value);
}

bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value) {
  return thread_pal_atomic_compare_exchange_i64(ptr, expected, value);
}

i64 thread_atomic_add_i64(i64* ptr, i64 value) { return thread_pal_atomic_add_i64(ptr, value); }

i64 thread_atomic_sub_i64(i64* ptr, i64 value) { return thread_pal_atomic_sub_i64(ptr, value); }

ThreadHandle thread_start(ThreadRoutine routine, void* data, String threadName) {
  ThreadRunData* threadRunData = alloc_alloc_t(g_alloc_heap, ThreadRunData);
  threadRunData->threadName    = string_dup(g_alloc_heap, threadName);
  threadRunData->userRoutine   = routine;
  threadRunData->userData      = data;
  return thread_pal_start(thread_runner, threadRunData);
}

void thread_join(ThreadHandle thread) { thread_pal_join(thread); }

void thread_yield() { thread_pal_yield(); }

void thread_sleep(const TimeDuration duration) { thread_pal_sleep(duration); }

ThreadMutex thread_mutex_create(Allocator* alloc) { return thread_pal_mutex_create(alloc); }

void thread_mutex_destroy(ThreadMutex mutex) { thread_pal_mutex_destroy(mutex); }

void thread_mutex_lock(ThreadMutex mutex) { thread_pal_mutex_lock(mutex); }

bool thread_mutex_trylock(ThreadMutex mutex) { return thread_pal_mutex_trylock(mutex); }

void thread_mutex_unlock(ThreadMutex mutex) { thread_pal_mutex_unlock(mutex); }

ThreadCondition thread_cond_create(Allocator* alloc) { return thread_pal_cond_create(alloc); }

void thread_cond_destroy(ThreadCondition cond) { thread_pal_cond_destroy(cond); }

void thread_cond_wait(ThreadCondition cond, ThreadMutex mutex) {
  thread_pal_cond_wait(cond, mutex);
}

void thread_cond_signal(ThreadCondition cond) { thread_pal_cond_signal(cond); }

void thread_cond_broadcast(ThreadCondition cond) { thread_pal_cond_broadcast(cond); }

void thread_spinlock_lock(ThreadSpinLock* lock) {
  /**
   * Naive implemenation of a general-purpose spin-lock using atomic operations. If required a much
   * faster architecture specific routine can be implemented.
   *
   * Includes a general memory barrier that synchronizes with 'thread_spinlock_unlock' because both
   * write to the same memory with sequentially-consistent ordering semantics.
   */
  i64 expected = 0;
  while (!thread_atomic_compare_exchange_i64(lock, &expected, 1)) {
    _mm_pause();
    expected = 0;
  }
}

void thread_spinlock_unlock(ThreadSpinLock* lock) {
  /**
   * Includes a general memory barrier that synchronizes with 'thread_spinlock_lock' because both
   * write to the same memory with sequentially-consistent ordering semantics.
   */
  thread_atomic_store_i64(lock, 0);
}
