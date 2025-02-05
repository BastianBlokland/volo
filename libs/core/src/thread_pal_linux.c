#include "core_alloc.h"
#include "core_diag.h"
#include "core_time.h"

#include "thread_internal.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#define thread_early_crash_exit_code 2

/**
 * The nice value determines the priority of processes / threads. The higher the value, the lower
 * the priority (the "nicer" the process is to other processes). The default nice value is 0.
 *
 * NOTE: Raising priority (negative nice values) usually requires elevated permissions.
 *
 * Docs: https://man7.org/linux/man-pages/man7/sched.7.html
 */
static int thread_desired_nice(const ThreadPriority prio) {
  switch (prio) {
  case ThreadPriority_Lowest:
    // NOTE: Linux defines 19 as the absolute lowest priority.
    return 10;
  case ThreadPriority_Low:
    return 5;
  case ThreadPriority_Normal:
    return 0;
  case ThreadPriority_High:
    return -5;
  case ThreadPriority_Highest:
    // NOTE: Linux defines -20 as the absolute highest priority.
    return -10;
  }
  diag_crash_msg("Unsupported thread-priority: {}", fmt_int(prio));
}

/**
 * Crude crash utility that can be used during early initialization before the allocators and the
 * normal crash infrastructure has been initialized.
 */
static NORETURN void thread_crash_early_init(const String msg) {
  MAYBE_UNUSED const usize bytesWritten = write(2, msg.ptr, msg.size);

  // NOTE: exit_group to terminate all threads in the process.
  syscall(SYS_exit_group, thread_early_crash_exit_code);
  UNREACHABLE
}

void thread_pal_init(void) {}
void thread_pal_init_late(void) {}
void thread_pal_teardown(void) {}

ASSERT(sizeof(ThreadId) >= sizeof(pid_t), "ThreadId type too small")

ThreadId thread_pal_pid(void) { return (ThreadId)syscall(SYS_getpid); }
ThreadId thread_pal_tid(void) { return (ThreadId)syscall(SYS_gettid); }

u16 thread_pal_core_count(void) {
  /**
   * NOTE: Called during early startup so cannot allocate memory.
   */
  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  const int res = sched_getaffinity(0, sizeof(cpuSet), &cpuSet);
  if (UNLIKELY(res != 0)) {
    thread_crash_early_init(string_lit("sched_getaffinity() failed\n"));
  }
  return CPU_COUNT(&cpuSet);
}

uptr thread_pal_stack_top(void) {
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr)) {
    diag_crash_msg("pthread_getattr_np() failed");
  }
  void*  stackPtr;
  size_t stackSize;
  if (pthread_attr_getstack(&attr, &stackPtr, &stackSize)) {
    diag_crash_msg("pthread_attr_getstack() failed");
  }
  if (pthread_attr_destroy(&attr)) {
    diag_crash_msg("pthread_attr_destroy() failed");
  }
  return (uptr)stackPtr + (uptr)stackSize;
}

void thread_pal_set_name(const String str) {
  static const usize g_maxNameLen = 15;
  if (str.size > g_maxNameLen) {
    diag_assert_fail(
        "Thread name '{}' is too long, maximum is {} chars", fmt_text(str), fmt_int(g_maxNameLen));
  }

  // Copy the string on the stack and null-terminate it.
  Mem buffer = mem_stack(str.size + 1);
  mem_cpy(buffer, str);
  *mem_at_u8(buffer, str.size) = '\0';

  const int res = prctl(PR_SET_NAME, (unsigned long)buffer.ptr, 0UL, 0UL, 0UL);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("prctl(PR_SET_NAME) failed");
  }
}

bool thread_pal_set_priority(const ThreadPriority prio) {
  /**
   * The process is ran under the 'SCHED_OTHER' (sometimes called 'SCHED_NORMAL') time sharing
   * scheduler which does not use a static scheduling priority (eg 'sched_priority') but instead
   * uses the thread's nice value as a dynamic priority.
   *
   * NOTE: POSIX only defines nice values for processes (not for threads), but Linux does support
   * per-thread nice values luckily.
   *
   * NOTE: Raising priority (negative nice values) usually requires elevated permissions.
   *
   * Docs: https://man7.org/linux/man-pages/man7/sched.7.html
   */
  const id_t tid  = (id_t)thread_pal_tid();
  const int  nice = thread_desired_nice(prio);
  const int  res  = setpriority(PRIO_PROCESS, tid, nice);
  if (res != 0) {
    if (errno == EACCES) {
      return false; // Insufficient permissions.
    }
    diag_crash_msg("setpriority() failed: {} (errno: {})", fmt_int(res), fmt_int(errno));
  }
  return true;
}

i32 thread_atomic_load_i32(i32* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
u32 thread_atomic_load_u32(u32* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
i64 thread_atomic_load_i64(i64* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
u64 thread_atomic_load_u64(u64* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }

void thread_atomic_store_i32(i32* ptr, i32 value) { __atomic_store(ptr, &value, __ATOMIC_SEQ_CST); }
void thread_atomic_store_u32(u32* ptr, u32 value) { __atomic_store(ptr, &value, __ATOMIC_SEQ_CST); }
void thread_atomic_store_i64(i64* ptr, i64 value) { __atomic_store(ptr, &value, __ATOMIC_SEQ_CST); }
void thread_atomic_store_u64(u64* ptr, u64 value) { __atomic_store(ptr, &value, __ATOMIC_SEQ_CST); }

i32 thread_atomic_exchange_i32(i32* ptr, const i32 value) {
  return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

i64 thread_atomic_exchange_i64(i64* ptr, const i64 value) {
  return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

bool thread_atomic_compare_exchange_i32(i32* ptr, i32* expected, const i32 value) {
  return __atomic_compare_exchange_n(
      ptr, expected, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, const i64 value) {
  return __atomic_compare_exchange_n(
      ptr, expected, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

i32 thread_atomic_add_i32(i32* ptr, const i32 value) {
  return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

i64 thread_atomic_add_i64(i64* ptr, const i64 value) {
  return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

i32 thread_atomic_sub_i32(i32* ptr, const i32 value) {
  return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

i64 thread_atomic_sub_i64(i64* ptr, const i64 value) {
  return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

ThreadHandle thread_pal_start(thread_pal_rettype(SYS_DECL* routine)(void*), void* data) {
  pthread_attr_t attr;
  pthread_t      handle;

  int res = pthread_attr_init(&attr);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_attr_init() failed: {}", fmt_int(res));
  }

  res = pthread_attr_setstacksize(&attr, thread_pal_stacksize);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_attr_setstacksize() failed: {}", fmt_int(res));
  }

  res = pthread_create(&handle, &attr, routine, data);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_create() failed: {}", fmt_int(res));
  }

  res = pthread_attr_destroy(&attr);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_attr_destroy() failed: {}", fmt_int(res));
  }

  ASSERT(sizeof(ThreadHandle) >= sizeof(pthread_t), "'pthread_t' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(const ThreadHandle thread) {
  void*     retData;
  const int res = pthread_join((pthread_t)thread, &retData);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_join() failed: {}", fmt_int(res));
  }
}

void thread_pal_yield(void) {
  /**
   * Because we are running under the normal time sharing scheduler ('SCHED_OTHER') the utility of
   * this is questionable and we should probably revisit the usages of this api.
   *
   * Docs: https://man7.org/linux/man-pages/man2/sched_yield.2.html
   */
  const int res = sched_yield();
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("sched_yield() failed: {}", fmt_int(res));
  }
}

void thread_pal_sleep(const TimeDuration duration) {
  struct timespec ts = {
      .tv_sec  = duration / time_second,
      .tv_nsec = duration % time_second,
  };
  int res = 0;
  while ((res = nanosleep(&ts, &ts)) == -1 && errno == EINTR) // Resume waiting after interupt.
    ;
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("nanosleep() failed: {}", fmt_int(res));
  }
}

bool thread_pal_exists(const ThreadId tid) {
  const pid_t pid = (pid_t)syscall(SYS_getpid);
  do {
    if (tgkill(pid, (pid_t)tid, 0) == 0) {
      return true; // Signal could be delivered.
    }
  } while (errno == EAGAIN);
  return false; // Signal could not be delivered.
}

typedef struct {
  pthread_mutex_t impl;
  Allocator*      alloc;
} ThreadMutexData;

ThreadMutex thread_mutex_create(Allocator* alloc) {
  pthread_mutexattr_t attr;
  int                 res = pthread_mutexattr_init(&attr);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutexattr_init() failed: {}", fmt_int(res));
  }

  res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutexattr_settype() failed: {}", fmt_int(res));
  }

  res = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_STALLED);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutexattr_setrobust() failed: {}", fmt_int(res));
  }

  ThreadMutexData* data = alloc_alloc_t(alloc, ThreadMutexData);
  data->alloc           = alloc;

  res = pthread_mutex_init(&data->impl, &attr);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutex_init() failed: {}", fmt_int(res));
  }

  res = pthread_mutexattr_destroy(&attr);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutexattr_destroy() failed: {}", fmt_int(res));
  }

  return (ThreadMutex)data;
}

void thread_mutex_destroy(const ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  const int res = pthread_mutex_destroy(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutex_destroy() failed: {}", fmt_int(res));
  }
  alloc_free_t(data->alloc, data);
}

void thread_mutex_lock(const ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  const int res = pthread_mutex_lock(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutex_lock() failed: {}", fmt_int(res));
  }
}

bool thread_mutex_trylock(const ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  const int res = pthread_mutex_trylock(&data->impl);
  if (res != 0 && res != EBUSY) {
    diag_crash_msg("pthread_mutex_trylock() failed: {}", fmt_int(res));
  }
  return res == 0;
}

void thread_mutex_unlock(const ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  const int res = pthread_mutex_unlock(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_mutex_unlock() failed: {}", fmt_int(res));
  }
}

typedef struct {
  pthread_cond_t impl;
  Allocator*     alloc;
} ThreadConditionData;

ThreadCondition thread_cond_create(Allocator* alloc) {
  ThreadConditionData* data = alloc_alloc_t(alloc, ThreadConditionData);
  data->alloc               = alloc;

  int res = pthread_cond_init(&data->impl, null);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_init() failed: {}", fmt_int(res));
  }
  return (ThreadCondition)data;
}

void thread_cond_destroy(const ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  const int res = pthread_cond_destroy(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_destroy() failed: {}", fmt_int(res));
  }
  alloc_free_t(data->alloc, data);
}

void thread_cond_wait(const ThreadCondition condHandle, const ThreadMutex mutexHandle) {
  ThreadConditionData* condData  = (ThreadConditionData*)condHandle;
  ThreadMutexData*     mutexData = (ThreadMutexData*)mutexHandle;

  const int res = pthread_cond_wait(&condData->impl, &mutexData->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_wait() failed: {}", fmt_int(res));
  }
}

void thread_cond_wait_timeout(
    const ThreadCondition condHandle, const ThreadMutex mutexHandle, const TimeDuration timeout) {
  ThreadConditionData* condData  = (ThreadConditionData*)condHandle;
  ThreadMutexData*     mutexData = (ThreadMutexData*)mutexHandle;

  int             res;
  struct timespec ts;

  res = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("clock_gettime(CLOCK_MONOTONIC) failed: {}", fmt_int(res));
  }

  const u64 seconds     = timeout / time_second;
  const u64 nanoSeconds = (timeout - time_seconds(seconds)) / time_nanosecond;

  ts.tv_sec += seconds;
  ts.tv_nsec += nanoSeconds;

  res = pthread_cond_clockwait(&condData->impl, &mutexData->impl, CLOCK_MONOTONIC, &ts);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_clockwait() failed: {}", fmt_int(res));
  }
}

void thread_cond_signal(const ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  const int res = pthread_cond_signal(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_signal() failed: {}", fmt_int(res));
  }
}

void thread_cond_broadcast(const ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  const int res = pthread_cond_broadcast(&data->impl);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("pthread_cond_broadcast() failed: {}", fmt_int(res));
  }
}
