#include "core_diag.h"
#include "core_init.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_tty.h"
#include "jobs_executor.h"
#include "jobs_init.h"

void test_dot();
void test_graph();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  core_init();
  jobs_init();

  tty_set_window_title(
      fmt_write_scratch("{}: running tests...", fmt_text(path_stem(g_path_executable))));

  diag_print(
      "{}: running tests... (pid: {}, workers: {})\n",
      fmt_text(path_stem(g_path_executable)),
      fmt_int(g_thread_pid),
      fmt_int(g_jobsWorkerCount));

  const TimeSteady timeStart = time_steady_clock();

  test_dot();
  test_graph();

  diag_print(
      "{}: passed, time: {}\n",
      fmt_text(path_stem(g_path_executable)),
      fmt_duration(time_steady_duration(timeStart, time_steady_clock())));

  jobs_teardown();
  core_teardown();
  return 0;
}
