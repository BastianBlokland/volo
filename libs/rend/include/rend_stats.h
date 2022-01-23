#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "rend_size.h"

ecs_comp_extern_public(RendStatsComp) {
  String       gpuName;
  RendSize     renderResolution;
  TimeDuration renderTime;
  u32          draws, instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
  u64          ramOccupied, ramReserved;
  u64          vramOccupied, vramReserved;
  u32          descSetsOccupied, descSetsReserved, descLayouts;
};
