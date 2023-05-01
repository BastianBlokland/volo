#pragma once
#include "ecs_entity.h"
#include "input_resource.h"

#define input_resource_max_maps 2

EcsEntityId input_resource_map(const InputResourceComp*);
