#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendLimiterComp) {
  TimeDuration sleepDur;
  u16          freq;
  TimeSteady   previousTime;
  TimeDuration sleepOverhead;
};
