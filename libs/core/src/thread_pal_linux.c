#include "core_diag.h"
#include "thread_internal.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

i64 thread_pal_pid() { return syscall(SYS_getpid); }
i64 thread_pal_tid() { return syscall(SYS_gettid); }

u16 thread_pal_core_count() {
  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  const int res = sched_getaffinity(0, sizeof(cpuSet), &cpuSet);
  diag_assert_msg(res == 0, fmt_write_scratch("sched_getaffinity() failed: {}", fmt_int(res)));
  (void)res;
  return CPU_COUNT(&cpuSet);
}

void thread_pal_set_name(const String str) {
  static const usize maxNameLen = 15;
  if (str.size > maxNameLen) {
    diag_assert_fail(
        &diag_callsite_create(),
        fmt_write_scratch(
            "Thread name '{}' is too long, maximum is {} chars",
            fmt_text(str),
            fmt_int(maxNameLen)));
  }

  // Copy the string on the stack and null-terminate it.
  Mem buffer = mem_stack(str.size + 1);
  mem_cpy(buffer, str);
  *mem_at_u8(buffer, str.size) = '\0';

  const pthread_t curThread = pthread_self();
  const int       res       = pthread_setname_np(curThread, buffer.ptr);
  diag_assert_msg(res == 0, string_lit("pthread_setname_np() failed"));
  (void)res;
}

i64 thread_pal_atomic_load_i64(i64* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }

void thread_pal_atomic_store_i64(i64* ptr, i64 value) {
  __atomic_store(ptr, &value, __ATOMIC_SEQ_CST);
}

i64 thread_pal_atomic_exchange_i64(i64* ptr, i64 value) {
  return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
}

bool thread_pal_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value) {
  return __atomic_compare_exchange_n(
      ptr, expected, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

i64 thread_pal_atomic_add_i64(i64* ptr, i64 value) {
  return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

i64 thread_pal_atomic_sub_i64(i64* ptr, i64 value) {
  return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

ThreadHandle thread_pal_start(thread_pal_rettype (*routine)(void*), void* data) {
  pthread_attr_t attr;
  pthread_t      handle;

  int res = pthread_attr_init(&attr);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_attr_init() failed: {}", fmt_int(res)));

  res = pthread_attr_setstacksize(&attr, thread_pal_stacksize);
  diag_assert_msg(
      res == 0, fmt_write_scratch("pthread_attr_setstacksize() failed: {}", fmt_int(res)));

  res = pthread_create(&handle, &attr, routine, data);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_create() failed: {}", fmt_int(res)));

  res = pthread_attr_destroy(&attr);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_attr_destroy() failed: {}", fmt_int(res)));

  (void)res;

  _Static_assert(sizeof(ThreadHandle) >= sizeof(pthread_t), "'pthread_t' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(ThreadHandle thread) {
  void*     retData;
  const int res = pthread_join((pthread_t)thread, &retData);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_join() failed: {}", fmt_int(res)));
  (void)res;
}

void thread_pal_yield() {
  const int res = sched_yield();
  diag_assert_msg(res == 0, fmt_write_scratch("sched_yield() failed: {}", fmt_int(res)));
  (void)res;
}

void thread_pal_sleep(const TimeDuration duration) {
  struct timespec ts = {
      .tv_sec  = duration / time_second,
      .tv_nsec = duration % time_second,
  };
  int res = 0;
  while ((res = nanosleep(&ts, &ts)) == -1 && errno == EINTR) // Resume waiting after interupt.
    ;
  diag_assert_msg(res == 0, fmt_write_scratch("nanosleep() failed: {}", fmt_int(res)));
}

typedef struct {
  Allocator* alloc;
  Mem        allocation;
} ThreadMutexExtraData;

ThreadMutex thread_pal_mutex_create(Allocator* alloc) {
  pthread_mutexattr_t attr;
  int                 res = pthread_mutexattr_init(&attr);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_mutexattr_init() failed: {}", fmt_int(res)));

  res = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
  diag_assert_msg(
      res == 0, fmt_write_scratch("pthread_mutexattr_settype() failed: {}", fmt_int(res)));

  res = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_STALLED);
  diag_assert_msg(
      res == 0, fmt_write_scratch("pthread_mutexattr_setrobust() failed: {}", fmt_int(res)));

  Mem allocation = alloc_alloc(alloc, sizeof(pthread_mutex_t) + sizeof(ThreadMutexExtraData));
  pthread_mutex_t* mutex = mem_as_t(allocation, pthread_mutex_t);

  *mem_as_t(mem_consume(allocation, sizeof(pthread_mutex_t)), ThreadMutexExtraData) =
      (ThreadMutexExtraData){
          .alloc      = alloc,
          .allocation = allocation,
      };

  res = pthread_mutex_init(mutex, &attr);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_mutex_init() failed: {}", fmt_int(res)));

  res = pthread_mutexattr_destroy(&attr);
  diag_assert_msg(
      res == 0, fmt_write_scratch("pthread_mutexattr_destroy() failed: {}", fmt_int(res)));

  (void)res;
  return (ThreadMutex)allocation.ptr;
}

void thread_pal_mutex_destroy(ThreadMutex handle) {
  pthread_mutex_t* mutex = (pthread_mutex_t*)handle;

  const int res = pthread_mutex_destroy(mutex);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_mutex_destroy() failed: {}", fmt_int(res)));
  (void)res;

  ThreadMutexExtraData* extraData = (ThreadMutexExtraData*)((u8*)handle + sizeof(pthread_mutex_t));
  alloc_free(extraData->alloc, extraData->allocation);
}

void thread_pal_mutex_lock(ThreadMutex handle) {
  pthread_mutex_t* mutex = (pthread_mutex_t*)handle;

  const int res = pthread_mutex_lock(mutex);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_mutex_lock() failed: {}", fmt_int(res)));
  (void)res;
}

bool thread_pal_mutex_trylock(ThreadMutex handle) {
  pthread_mutex_t* mutex = (pthread_mutex_t*)handle;

  const int res = pthread_mutex_trylock(mutex);
  diag_assert_msg(
      res == 0 || res == EBUSY,
      fmt_write_scratch("pthread_mutex_trylock() failed: {}", fmt_int(res)));
  return res == 0;
}

void thread_pal_mutex_unlock(ThreadMutex handle) {
  pthread_mutex_t* mutex = (pthread_mutex_t*)handle;

  const int res = pthread_mutex_unlock(mutex);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_mutex_unlock() failed: {}", fmt_int(res)));
  (void)res;
}

typedef struct {
  Allocator* alloc;
  Mem        allocation;
} ThreadConditionExtraData;

ThreadCondition thread_pal_cond_create(Allocator* alloc) {
  Mem allocation = alloc_alloc(alloc, sizeof(pthread_cond_t) + sizeof(ThreadConditionExtraData));
  pthread_cond_t* cond = mem_as_t(allocation, pthread_cond_t);

  *mem_as_t(mem_consume(allocation, sizeof(pthread_cond_t)), ThreadConditionExtraData) =
      (ThreadConditionExtraData){
          .alloc      = alloc,
          .allocation = allocation,
      };

  int res = pthread_cond_init(cond, null);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_cond_init() failed: {}", fmt_int(res)));

  (void)res;
  return (ThreadCondition)allocation.ptr;
}

void thread_pal_cond_destroy(ThreadCondition handle) {
  pthread_cond_t* cond = (pthread_cond_t*)handle;

  const int res = pthread_cond_destroy(cond);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_cond_destroy() failed: {}", fmt_int(res)));
  (void)res;

  ThreadConditionExtraData* extraData =
      (ThreadConditionExtraData*)((u8*)handle + sizeof(pthread_cond_t));
  alloc_free(extraData->alloc, extraData->allocation);
}

void thread_pal_cond_wait(ThreadCondition condHandle, ThreadMutex mutexHandle) {
  pthread_cond_t*  cond  = (pthread_cond_t*)condHandle;
  pthread_mutex_t* mutex = (pthread_mutex_t*)mutexHandle;

  const int res = pthread_cond_wait(cond, mutex);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_cond_wait() failed: {}", fmt_int(res)));
  (void)res;
}

void thread_pal_cond_signal(ThreadCondition handle) {
  pthread_cond_t* cond = (pthread_cond_t*)handle;

  const int res = pthread_cond_signal(cond);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_cond_signal() failed: {}", fmt_int(res)));
  (void)res;
}

void thread_pal_cond_broadcast(ThreadCondition handle) {
  pthread_cond_t* cond = (pthread_cond_t*)handle;

  const int res = pthread_cond_broadcast(cond);
  diag_assert_msg(res == 0, fmt_write_scratch("pthread_cond_broadcast() failed: {}", fmt_int(res)));
  (void)res;
}
