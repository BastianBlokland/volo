#include "core_alloc.h"
#include "core_thread.h"

#include "init_internal.h"
#include "thread_internal.h"

#include <immintrin.h>

typedef struct {
  String         threadName;
  ThreadPriority threadPriority;
  ThreadRoutine  userRoutine;
  void*          userData;
} ThreadRunData;

static thread_pal_rettype SYS_DECL thread_runner(void* data) {
  ThreadRunData* runData = (ThreadRunData*)data;

  // Initialize the core library for this thread.
  core_init();

  // Initialize the thread name.
  g_thread_name = runData->threadName;
  thread_pal_set_name(g_thread_name);

  // Set the thread priority.
  if (runData->threadPriority != ThreadPriority_Normal) {
    thread_pal_set_priority(runData->threadPriority); // NOTE: Can fail due to insufficient perms.
  }

  // Invoke the user routine.
  runData->userRoutine(runData->userData);

  // Tear-down the core library for this thread.
  core_teardown();

  // Cleanup thread data.
  string_free(g_alloc_heap, runData->threadName);
  alloc_free_t(g_alloc_heap, runData);

  return (thread_pal_rettype)0;
}

ThreadId              g_thread_pid;
ThreadId              g_thread_main_tid;
THREAD_LOCAL ThreadId g_thread_tid;
THREAD_LOCAL String   g_thread_name;
u16                   g_thread_core_count;

void thread_init(void) {
  thread_pal_init();

  g_thread_pid        = thread_pal_pid();
  g_thread_main_tid   = thread_pal_tid();
  g_thread_name       = string_lit("volo_main");
  g_thread_core_count = thread_pal_core_count();
}

void thread_init_late(void) {
  thread_pal_init_late();

  thread_pal_set_name(g_thread_name);
}

void thread_teardown(void) { thread_pal_teardown(); }

void thread_init_thread(void) { g_thread_tid = thread_pal_tid(); }

ThreadHandle thread_start(
    ThreadRoutine routine, void* data, const String threadName, const ThreadPriority prio) {
  ThreadRunData* threadRunData  = alloc_alloc_t(g_alloc_heap, ThreadRunData);
  threadRunData->threadName     = string_dup(g_alloc_heap, threadName);
  threadRunData->threadPriority = prio;
  threadRunData->userRoutine    = routine;
  threadRunData->userData       = data;
  return thread_pal_start(thread_runner, threadRunData);
}

bool thread_prioritize(const ThreadPriority prio) { return thread_pal_set_priority(prio); }

void thread_join(const ThreadHandle thread) { thread_pal_join(thread); }

void thread_yield(void) { thread_pal_yield(); }

void thread_sleep(const TimeDuration duration) { thread_pal_sleep(duration); }

bool thread_exists(const ThreadId tid) { return thread_pal_exists(tid); }

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
   * Naive implementation of a general-purpose spin-lock using atomic operations. If required a much
   * faster architecture specific routine can be implemented.
   *
   * Includes a general memory barrier that synchronizes with 'thread_spinlock_unlock' because both
   * write to the same memory with sequentially-consistent ordering semantics.
   */
  i32 expected = 0;
  while (!thread_atomic_compare_exchange_i32(lock, &expected, 1)) {
    _mm_pause();
    expected = 0;
  }
}

void thread_spinlock_unlock(ThreadSpinLock* lock) {
  /**
   * Includes a general memory barrier that synchronizes with 'thread_spinlock_lock' because both
   * write to the same memory with sequentially-consistent ordering semantics.
   */
  thread_atomic_store_i32(lock, 0);
}
