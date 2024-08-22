#pragma once
#include "core_time.h"
#include "ecs_module.h"

#define rend_stats_max_passes 16

typedef enum {
  RendStatRes_Graphic,
  RendStatRes_Shader,
  RendStatRes_Mesh,
  RendStatRes_Texture,

  RendStatRes_Count,
} RendStatRes;

typedef struct {
  String       name; // Persistently allocated.
  TimeDuration gpuExecDur;
  u16          sizeMax[2];
  u16          invocations;
  u16          draws;
  u32          instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
} RendStatPass;

ecs_comp_extern_public(RendStatsComp) {
  String gpuName;

  TimeDuration waitForGpuDur; // Time the cpu was blocked waiting for the gpu.
  TimeDuration gpuExecDur;
  TimeDuration presentAcquireDur, presentEnqueueDur, presentWaitDur;
  TimeDuration limiterDur;

  u32           passCount;
  RendStatPass* passes; // RendStatPass[rend_stats_max_passes];

  u64 swapchainPresentId;
  u16 swapchainImageCount;
  u16 memChunks;
  u64 ramOccupied, ramReserved;
  u64 vramOccupied, vramReserved;
  u16 descSetsOccupied, descSetsReserved, descLayouts;
  u16 attachCount;
  u64 attachMemory;
  u16 samplerCount;
  u16 resources[RendStatRes_Count];
};
