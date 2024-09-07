#pragma once
#include "asset_graphic.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkDevice   RvkDevice;
typedef struct sRvkPass     RvkPass;
typedef struct sRendBuilder RendBuilder;

ecs_comp_extern_public(RendPlatformComp) {
  RvkDevice*   device;
  RendBuilder* builder;
  RvkPass*     passes[AssetGraphicPass_Count];
};

void rend_platform_teardown(EcsWorld*);
