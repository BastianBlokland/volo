#include "core_alloc.h"
#include "core_diag.h"
#include "core_init.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"

void test_alloc_bump();
void test_ascii();
void test_bits();
void test_bitset();
void test_compare();
void test_dynarray();
void test_dynbitset();
void test_dynstring();
void test_file();
void test_float();
void test_format();
void test_macro();
void test_math();
void test_memory();
void test_path();
void test_sort();
void test_string();
void test_thread();
void test_time();
void test_utf8();
void test_winutils();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  core_init();

  diag_print(
      "{}: running tests... (pid: {}, tid: {}, cpus: {}, pagesize: {})\n",
      fmt_text(path_stem(g_path_executable)),
      fmt_int(g_thread_pid),
      fmt_int(g_thread_tid),
      fmt_int(g_thread_core_count),
      fmt_int(alloc_min_size(g_alloc_page)));

  const TimeSteady timeStart = time_steady_clock();

  test_alloc_bump();
  test_ascii();
  test_bits();
  test_bitset();
  test_compare();
  test_dynarray();
  test_dynbitset();
  test_dynstring();
  test_file();
  test_float();
  test_format();
  test_macro();
  test_math();
  test_memory();
  test_path();
  test_sort();
  test_string();
  test_thread();
  test_time();
  test_utf8();
  test_winutils();

  diag_print(
      "{}: passed, time: {}\n",
      fmt_text(path_stem(g_path_executable)),
      fmt_duration(time_steady_duration(timeStart, time_steady_clock())));

  core_teardown();
  return 0;
}
