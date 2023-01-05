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

typedef struct {
  TimeDuration dur;
  u32          draws, instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
} RendStatPass;

ecs_comp_extern_public(RendStatsComp) {
  String gpuName;
  u16    renderSize[2];

  TimeDuration renderDur, waitForRenderDur;
  TimeDuration presentAcquireDur, presentEnqueueDur, presentWaitDur;
  TimeDuration limiterDur;

  RendStatPass passGeometry, passForward, passShadow, passAmbientOcclusion;

  u64 ramOccupied, ramReserved;
  u64 vramOccupied, vramReserved;
  u16 descSetsOccupied, descSetsReserved, descLayouts;
  u16 attachCount;
  u64 attachMemory;
  u16 resources[RendStatRes_Count];
};
