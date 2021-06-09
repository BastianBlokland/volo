#include "core_diag.h"
#include "thread_internal.h"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

i64 thread_pal_pid() { return getpid(); }
i64 thread_pal_tid() { return gettid(); }

u16 thread_pal_processor_count() {
  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  const int res = sched_getaffinity(0, sizeof(cpuSet), &cpuSet);
  diag_assert_msg(res == 0, string_lit("sched_getaffinity() failed"));
  (void)res;
  return CPU_COUNT(&cpuSet);
}

void thread_pal_set_name(const String str) {
  static const usize maxNameLen = 15;
  if (str.size > maxNameLen) {
    diag_assert_fail(
        &diag_callsite_create(), string_lit("Thread name too long, maximum is 15 chars"));
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

ThreadHandle thread_pal_start(thread_pal_rettype (*routine)(void*), void* data) {
  pthread_attr_t attr;
  pthread_t      handle;

  int res = pthread_attr_init(&attr);
  diag_assert_msg(res == 0, string_lit("pthread_attr_init() failed"));

  res = pthread_attr_setstacksize(&attr, thread_pal_stacksize);
  diag_assert_msg(res == 0, string_lit("pthread_attr_setstacksize() failed"));

  res = pthread_create(&handle, &attr, routine, data);
  diag_assert_msg(res == 0, string_lit("pthread_create() failed"));

  res = pthread_attr_destroy(&attr);
  diag_assert_msg(res == 0, string_lit("pthread_attr_destroy() failed"));

  (void)res;

  _Static_assert(sizeof(ThreadHandle) >= sizeof(pthread_t), "'pthread_t' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(ThreadHandle thread) {
  void*     retData;
  const int res = pthread_join((pthread_t)thread, &retData);
  diag_assert_msg(res == 0, string_lit("pthread_join() failed"));
  (void)res;
}
