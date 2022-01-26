#pragma once
#include "core_time.h"
#include "ecs_module.h"

typedef enum {
  SceneStatRes_Graphic,
  SceneStatRes_Shader,
  SceneStatRes_Mesh,
  SceneStatRes_Texture,

  SceneStatRes_Count,
} SceneStatRes;

ecs_comp_extern_public(SceneStatsCamComp) {
  String       gpuName;
  u32          renderResolution[2];
  TimeDuration renderTime;
  u32          draws, instances;
  u64          vertices, primitives;
  u64          shadersVert, shadersFrag;
  u64          ramOccupied, ramReserved;
  u64          vramOccupied, vramReserved;
  u32          descSetsOccupied, descSetsReserved, descLayouts;
  u32          resources[SceneStatRes_Count];
};
