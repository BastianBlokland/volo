#pragma once
#include "ecs_module.h"

#include "rvk/platform_internal.h"

ecs_comp_extern_public(RendPlatformComp) { RvkPlatform* vulkan; };

void rend_platform_teardown(EcsWorld*);
