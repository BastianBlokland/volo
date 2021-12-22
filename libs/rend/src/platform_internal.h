#pragma once
#include "ecs_module.h"

#include "rvk/device_internal.h"

ecs_comp_extern_public(RendPlatformComp) { RvkDevice* device; };

void rend_platform_teardown(EcsWorld*);
