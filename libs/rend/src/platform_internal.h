#pragma once
#include "asset_graphic.h"
#include "ecs_module.h"

#include "forward_internal.h"
#include "rvk/forward_internal.h"

ecs_comp_extern_public(RendPlatformComp) {
  RvkDevice*            device;
  RendBuilderContainer* builderContainer;
  RvkPass*              passes[AssetGraphicPass_Count];
};

void rend_platform_teardown(EcsWorld*);
