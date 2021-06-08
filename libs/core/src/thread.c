#include "core_thread.h"
#include "thread_internal.h"

i64              g_thread_pid;
i64              g_thread_main_tid;
THREAD_LOCAL i64 g_thread_tid;

void thread_init() {
  g_thread_pid      = thread_pal_pid();
  g_thread_tid      = thread_pal_tid();
  g_thread_main_tid = g_thread_tid;
  thread_pal_name_current(string_lit("volo_main"));
}
