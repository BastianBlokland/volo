#include "core_diag.h"
#include "core_init.h"
#include "core_thread.h"

static void test_thread(void* data) {
  (void)data;
  diag_print(
      "Hello from executor! (tid: {}, name: {})\n", fmt_int(g_thread_tid), fmt_text(g_thread_name));
}

int main() {
  core_init();

  diag_print(
      "Hello from main! (pid: {}, tid: {}, name: {})\n",
      fmt_int(g_thread_pid),
      fmt_int(g_thread_tid),
      fmt_text(g_thread_name));

  ThreadHandle exec = thread_start(test_thread, null, string_lit("volo_executor"));
  thread_join(exec);

  core_teardown();
  return 0;
}
