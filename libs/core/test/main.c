#include "core_alloc.h"
#include "core_diag.h"

void test_bits();
void test_bitset();
void test_compare();
void test_dynarray();
void test_dynbitset();
void test_dynstring();
void test_math();
void test_string();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  alloc_init();

  test_bits();
  test_bitset();
  test_compare();
  test_dynarray();
  test_dynbitset();
  test_dynstring();
  test_math();
  test_string();
  diag_log("volo_core_test: Passed\n");
  return 1;
}
