#include "core_diag.h"
#include "thread_internal.h"
#include <pthread.h>
#include <unistd.h>

i64 thread_pal_pid() { return getpid(); }

void thread_pal_name_current(const String str) {
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
