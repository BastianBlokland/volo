#pragma once
#include "ecs_module.h"

#include "vulkan/platform_internal.h"

ecs_comp_extern_public(RendPlatformComp) { RendVkPlatform* vulkan; };
