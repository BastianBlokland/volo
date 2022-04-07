#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(InputResourcesComp);

EcsEntityId input_resource_map(const InputResourcesComp*);
