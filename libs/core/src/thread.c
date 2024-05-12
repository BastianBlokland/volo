#include "core_alloc.h"
#include "core_diag_except.h"
#include "core_thread.h"

#include "init_internal.h"
#include "thread_internal.h"

#include <immintrin.h>

#ifdef VOLO_MSVC
#include <intrin.h>
#endif

typedef struct {
  String         threadName;
  ThreadPriority threadPriority;
  ThreadRoutine  userRoutine;
  void*          userData;
} ThreadRunData;

/**
 * Issue a compiler fence, does not emit any instructions but prevents the compiler from reordering
 * memory accesses.
 */
MAYBE_UNUSED INLINE_HINT static void thread_compiler_fence() {
#if defined(VOLO_CLANG) || defined(VOLO_GCC)
  asm volatile("" : : : "memory");
#elif defined(VOLO_MSVC)
  _ReadWriteBarrier();
#else
  ASSERT(false, "Unsupported compiler");
#endif
}

static thread_pal_rettype SYS_DECL thread_runner(void* data) {
  ThreadRunData* runData = (ThreadRunData*)data;

  core_init(); // Initialize the core library for this thread.

  jmp_buf exceptAnchor;
  diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));

  // Initialize the thread name.
  g_thread_name = runData->threadName;
  thread_pal_set_name(g_thread_name);

  // Set the thread priority.
  if (runData->threadPriority != ThreadPriority_Normal) {
    thread_pal_set_priority(runData->threadPriority); // NOTE: Can fail due to insufficient perms.
  }

  // Invoke the user routine.
  runData->userRoutine(runData->userData);

  diag_except_disable();
  core_teardown(); // Tear-down the core library for this thread.

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

void thread_atomic_fence(void) {
  // TODO: Experiment with issuing an instruction with a 'LOCK' prefix instead, this can potentially
  // be faster then the mfence instruction with the same semantics.
  _mm_mfence();
}

void thread_atomic_fence_acquire(void) {
  /**
   * NOTE: Does not need to emit any instructions on x86, if we ever port to ARM (or another
   * architecture with a weak memory model) it will need to emit instructions.
   */
  thread_compiler_fence();
}

void thread_atomic_fence_release(void) {
  /**
   * NOTE: Does not need to emit any instructions on x86, if we ever port to ARM (or another
   * architecture with a weak memory model) it will need to emit instructions.
   */
  thread_compiler_fence();
}

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

void thread_spinlock_lock(ThreadSpinLock* lock) {
  /**
   * Naive implementation of a general-purpose spin-lock using atomic operations.
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
