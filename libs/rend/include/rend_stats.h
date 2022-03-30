#pragma once
#include "core_time.h"
#include "ecs_module.h"

typedef enum {
  RendStatRes_Graphic,
  RendStatRes_Shader,
  RendStatRes_Mesh,
  RendStatRes_Texture,

  RendStatRes_Count,
} RendStatRes;

ecs_comp_extern_public(RendStatsComp) {
  String       gpuName;
  u32          renderSize[2];
  TimeDuration renderDur, waitForRenderDur;
  TimeDuration presentAcquireDur, presentEnqueueDur;
  TimeDuration limiterDur;
  u32          draws, instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
  u64          ramOccupied, ramReserved;
  u64          vramOccupied, vramReserved;
  u32          descSetsOccupied, descSetsReserved, descLayouts;
  u32          resources[RendStatRes_Count];
};
