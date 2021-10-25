#pragma once
#include "ecs_module.h"

#include "vulkan/context_internal.h"

ecs_comp_extern_public(RendPlatformComp) { RendVkContext* vulkan; };
