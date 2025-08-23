#include "core/alloc.h"
#include "core/diag_except.h"
#include "core/string.h"
#include "core/thread.h"

#include "init_internal.h"
#include "thread_internal.h"

#include <immintrin.h>

#ifdef VOLO_MSVC
#include <Windows.h>
#include <intrin.h>
#endif

#define VOLO_THREAD_X86

typedef struct {
  String         threadName;
  ThreadPriority threadPriority;
  ThreadRoutine  userRoutine;
  void*          userData;
} ThreadRunData;

static thread_pal_rettype SYS_DECL thread_runner(void* data) {
  ThreadRunData* runData = (ThreadRunData*)data;

  core_init(); // Initialize the core library for this thread.
  g_threadManaged = true;

  jmp_buf exceptAnchor;
  diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));

  // Initialize the thread name.
  g_threadName = runData->threadName;
  thread_pal_set_name(g_threadName);

  // Set the thread priority.
  if (runData->threadPriority != ThreadPriority_Normal) {
    thread_pal_set_priority(runData->threadPriority); // NOTE: Can fail due to insufficient perms.
  }

  // Invoke the user routine.
  runData->userRoutine(runData->userData);

  diag_except_disable();
  core_teardown(); // Tear-down the core library for this thread.

  // Cleanup thread data.
  string_free(g_allocHeap, runData->threadName);
  alloc_free_t(g_allocHeap, runData);

  return (thread_pal_rettype)0;
}

ThreadId              g_threadPid;
ThreadId              g_threadMainTid;
THREAD_LOCAL bool     g_threadManaged;
THREAD_LOCAL ThreadId g_threadTid;
THREAD_LOCAL String   g_threadName;
THREAD_LOCAL uptr     g_threadStackTop;
u16                   g_threadCoreCount;

void thread_init(void) {
  /**
   * Early main-thread initialization.
   * NOTE: Returns before memory allocators have been setup so cannot allocate any memory.
   */
  thread_pal_init();

  g_threadPid       = thread_pal_pid();
  g_threadMainTid   = thread_pal_tid();
  g_threadName      = string_lit("volo_main");
  g_threadCoreCount = thread_pal_core_count();
}

void thread_init_late(void) {
  /**
   * Late main-thread initialization.
   * NOTE: Memory can now be allocated.
   */
  thread_pal_init_late();

  thread_pal_set_name(g_threadName);
}

void thread_teardown(void) { thread_pal_teardown(); }

void thread_init_thread(void) {
  /**
   * Early thread initialization (not just the main-thread like 'thread_init()').
   * NOTE: Called during early startup so cannot allocate memory.
   */
  g_threadTid = thread_pal_tid();
}

void thread_init_thread_late(void) {
  /**
   * Late thread initialization (not just the main-thread like 'thread_init_late()').
   * NOTE: Memory can now be allocated.
   */
  g_threadStackTop = thread_pal_stack_top();
}

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
  COMPILER_BARRIER();
}

void thread_atomic_fence_release(void) {
  /**
   * NOTE: Does not need to emit any instructions on x86, if we ever port to ARM (or another
   * architecture with a weak memory model) it will need to emit instructions.
   */
  COMPILER_BARRIER();
}

ThreadHandle thread_start(
    ThreadRoutine routine, void* data, const String threadName, const ThreadPriority prio) {
  ThreadRunData* threadRunData  = alloc_alloc_t(g_allocHeap, ThreadRunData);
  threadRunData->threadName     = string_dup(g_allocHeap, threadName);
  threadRunData->threadPriority = prio;
  threadRunData->userRoutine    = routine;
  threadRunData->userData       = data;
  return thread_pal_start(thread_runner, threadRunData);
}

void thread_ensure_init(void) {
  if (g_threadManaged) {
    return; // Managed threads don't need to be initialized.
  }
  static THREAD_LOCAL bool g_threadExternInit;
  if (!g_threadExternInit) {
    core_init(); // Initialize the core library for this external thread.
    // TODO: Teardown at thread exit.

    g_threadName       = string_lit("volo_extern");
    g_threadExternInit = true;
  }
}

bool thread_prioritize(const ThreadPriority prio) { return thread_pal_set_priority(prio); }

void thread_join(const ThreadHandle thread) { thread_pal_join(thread); }

void thread_yield(void) { thread_pal_yield(); }

void thread_sleep(const TimeDuration duration) { thread_pal_sleep(duration); }

bool thread_exists(const ThreadId tid) { return thread_pal_exists(tid); }

#if defined(VOLO_THREAD_X86)

void thread_spinlock_lock(ThreadSpinLock* lock) {
#if defined(VOLO_GCC) || defined(VOLO_CLANG)
  while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
    while (*lock) {
      _mm_pause();
    }
  }
#elif defined(VOLO_MSVC)
  while (InterlockedExchangeAcquire((LONG volatile*)lock, 1)) {
    while (*lock) {
      _mm_pause();
    }
  }
#else
#error Unsupported compiler
#endif
}

void thread_spinlock_unlock(ThreadSpinLock* lock) {
  COMPILER_BARRIER();
  *lock = 0;
}

#else // !VOLO_THREAD_X86

void thread_spinlock_lock(ThreadSpinLock* lock) {
  /**
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

#endif // !VOLO_THREAD_X86
