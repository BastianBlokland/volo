#include "core_diag.h"
#include "core_init.h"

void test_bits();
void test_bitset();
void test_compare();
void test_dynarray();
void test_dynbitset();
void test_dynstring();
void test_float();
void test_format();
void test_math();
void test_sort();
void test_string();
void test_time();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  core_init();

  test_bits();
  test_bitset();
  test_compare();
  test_dynarray();
  test_dynbitset();
  test_dynstring();
  test_float();
  test_format();
  test_math();
  test_sort();
  test_string();
  test_time();

  diag_log("volo_core_test: Passed\n");
  return 1;
}
