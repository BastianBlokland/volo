#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendStatsComp) {
  TimeDuration renderTime;
  u64          ramOccupied, ramReserved;
  u64          vramOccupied, vramReserved;
};
