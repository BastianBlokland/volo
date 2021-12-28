#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneTimeComp) {
  TimeDuration time, delta;
  u64          ticks;
};
