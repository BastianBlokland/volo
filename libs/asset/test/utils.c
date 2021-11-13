#include "utils_internal.h"

void asset_test_wait(EcsRunner* runner) {
  static const u32 numTicks = 3;
  for (u32 i = 0; i != numTicks; ++i) {
    ecs_run_sync(runner);
  }
}
