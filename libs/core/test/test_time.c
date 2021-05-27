#include "core_diag.h"
#include "core_time.h"

static void test_time_clocksteady() { diag_assert(time_clocksteady() > 0); }

void test_time() { test_time_clocksteady(); }
