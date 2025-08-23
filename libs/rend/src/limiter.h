#pragma once
#include "core/time.h"
#include "ecs/module.h"

ecs_comp_extern_public(RendLimiterComp) {
  TimeDuration sleepDur;
  TimeSteady   previousTime;
  TimeDuration sleepOverhead;
};
