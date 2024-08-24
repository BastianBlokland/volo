#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkDevice   RvkDevice;
typedef struct sRendBuilder RendBuilder;

ecs_comp_extern_public(RendPlatformComp) {
  RvkDevice*   device;
  RendBuilder* builder;
};

void rend_platform_teardown(EcsWorld*);
