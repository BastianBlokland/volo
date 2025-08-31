#pragma once
#include "input/resource.h"

#define input_resource_max_maps 4

u32 input_resource_maps(
    const InputResourceComp*, EcsEntityId out[PARAM_ARRAY_SIZE(input_resource_max_maps)]);
