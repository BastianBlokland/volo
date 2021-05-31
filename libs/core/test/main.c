#include "core_dynstring.h"
#include "core_file.h"
#include "core_format.h"
#include "core_init.h"
#include "core_time.h"

void test_bits();
void test_bitset();
void test_compare();
void test_dynarray();
void test_dynbitset();
void test_dynstring();
void test_file();
void test_float();
void test_format();
void test_math();
void test_sort();
void test_string();
void test_time();
void test_utf8();
void test_winutils();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  core_init();

  const TimeSteady timeStart = time_steady_clock();

  test_bits();
  test_bitset();
  test_compare();
  test_dynarray();
  test_dynbitset();
  test_dynstring();
  test_file();
  test_float();
  test_format();
  test_math();
  test_sort();
  test_string();
  test_time();
  test_utf8();
  test_winutils();

  const Duration duration = time_steady_duration(timeStart, time_steady_clock());

  DynString outBuffer = dynstring_create(g_allocator_heap, 512);
  dynstring_append(&outBuffer, string_lit("volo_core_test: passed, time: "));
  format_write_duration_pretty(&outBuffer, duration);
  dynstring_append(&outBuffer, string_lit("\n"));
  file_write_sync(g_file_stdout, dynstring_view(&outBuffer));
  dynstring_destroy(&outBuffer);

  core_teardown();
  return 1;
}
