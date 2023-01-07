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
  u16          size[2];
  u16          draws;
  u32          instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
} RendStatPass;

ecs_comp_extern_public(RendStatsComp) {
  String gpuName;

  TimeDuration renderDur, waitForRenderDur;
  TimeDuration presentAcquireDur, presentEnqueueDur, presentWaitDur;
  TimeDuration limiterDur;

  RendStatPass passGeometry, passForward, passPost, passShadow, passAo;

  u16 memChunks;
  u64 ramOccupied, ramReserved;
  u64 vramOccupied, vramReserved;
  u16 descSetsOccupied, descSetsReserved, descLayouts;
  u16 attachCount;
  u64 attachMemory;
  u16 resources[RendStatRes_Count];
};
