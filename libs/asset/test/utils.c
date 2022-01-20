#include "utils_internal.h"

void asset_test_wait(EcsRunner* runner) {
  static const u32 g_numTicks = 5;
  for (u32 i = 0; i != g_numTicks; ++i) {
    ecs_run_sync(runner);
  }
}
