#pragma once
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

ecs_comp_extern_public(RendPlatformComp) { RvkDevice* device; };
