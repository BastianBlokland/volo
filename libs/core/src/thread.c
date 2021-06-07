#include "core_thread.h"
#include "thread_internal.h"

i64 g_thread_pid;

void thread_init() {
  g_thread_pid = thread_pal_pid();
  thread_pal_name_current(string_lit("volo_main"));
}
