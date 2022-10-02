#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(VfxGlobalResourcesComp);

EcsEntityId vfx_resource_particle_graphic(const VfxGlobalResourcesComp*);
