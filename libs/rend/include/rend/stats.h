#pragma once
#include "core/time.h"
#include "ecs/module.h"

#define rend_stats_max_passes 16

typedef enum {
  RendStatsRes_Graphic,
  RendStatsRes_Shader,
  RendStatsRes_Mesh,
  RendStatsRes_Texture,

  RendStatsRes_Count,
} RendStatsRes;

typedef struct {
  String       name; // Persistently allocated.
  TimeDuration gpuExecDur;
  u16          sizeMax[2];
  u16          invocations;
  u16          draws;
  u32          instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
} RendStatsPass;

ecs_comp_extern_public(RendStatsComp) {
  String gpuName, gpuDriverName;

  TimeDuration waitForGpuDur; // Time the cpu was blocked waiting for the gpu.
  TimeDuration gpuWaitDur, gpuExecDur, gpuCopyDur;
  TimeDuration presentAcquireDur, presentEnqueueDur, presentWaitDur;
  TimeDuration limiterDur;

  bool profileSupported, profileTrigger;

  u32            passCount;
  RendStatsPass* passes; // RendStatsPass[rend_stats_max_passes];

  TimeDuration swapchainRefreshDuration;
  u16          swapchainImageCount;
  u16          memChunks;
  u64          ramOccupied, ramReserved;
  u64          vramOccupied, vramReserved;
  u64          vramBudgetTotal, vramBudgetUsed; // Optionally available if supported by the driver.
  u16          descSetsOccupied, descSetsReserved, descLayouts;
  u16          attachCount;
  u64          attachMemory;
  u16          samplerCount;
  u16          resources[RendStatsRes_Count];
};
