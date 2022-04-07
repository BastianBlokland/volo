#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(InputGlobalResourcesComp);

EcsEntityId input_resource_map(const InputGlobalResourcesComp*);
