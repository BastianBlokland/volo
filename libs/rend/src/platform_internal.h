#pragma once
#include "asset/graphic.h"
#include "ecs/module.h"

#include "forward_internal.h"
#include "rvk/forward_internal.h"

ecs_comp_extern_public(RendPlatformComp) {
  RvkLib*               lib;
  RvkDevice*            device;
  RendBuilderContainer* builderContainer;
  RvkPass*              passes[AssetGraphicPass_Count];
};

void rend_platform_teardown(EcsWorld*);
