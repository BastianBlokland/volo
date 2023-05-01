#pragma once
#include "ecs_entity.h"
#include "input_resource.h"

#define input_resource_max_maps 2

u32 input_resource_maps(
    const InputResourceComp*, EcsEntityId out[PARAM_ARRAY_SIZE(input_resource_max_maps)]);
