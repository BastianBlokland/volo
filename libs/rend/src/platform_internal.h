#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkDevice   RvkDevice;
typedef struct sRvkPass     RvkPass;
typedef struct sRendBuilder RendBuilder;

typedef enum {
  RendPassId_Geometry,
  RendPassId_Decal,
  RendPassId_Fog,
  RendPassId_FogBlur,
  RendPassId_Shadow,
  RendPassId_AmbientOcclusion,
  RendPassId_Forward,
  RendPassId_Distortion,
  RendPassId_Bloom,
  RendPassId_Post,

  RendPassId_Count,
} RendPassId;

ecs_comp_extern_public(RendPlatformComp) {
  RvkDevice*   device;
  RendBuilder* builder;
  RvkPass*     passes[RendPassId_Count];
};

void rend_platform_teardown(EcsWorld*);
