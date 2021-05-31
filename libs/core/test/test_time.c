#include "core_diag.h"
#include "core_time.h"

static void test_time_steady_clock() { diag_assert(time_steady_clock() > 0); }

void test_time() { test_time_steady_clock(); }
