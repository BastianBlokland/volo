#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendLimiterComp) {
  TimeDuration sleepDur;
  TimeSteady   previousTime;
  TimeDuration sleepOverhead;
};
