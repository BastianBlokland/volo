#include "core_diag.h"

void test_bits();
void test_dynarray();
void test_string();

/**
 * Run basic unit tests.
 * TODO: Should be moved to an actual unit testing framework at some point.
 */
int main() {
  test_bits();
  test_dynarray();
  test_string();
  diag_log("volo_core_test: Passed\n");
  return 1;
}
