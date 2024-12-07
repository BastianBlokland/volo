#pragma once
#include "input_resource.h"

#define input_resource_max_maps 3

u32 input_resource_maps(
    const InputResourceComp*, EcsEntityId out[PARAM_ARRAY_SIZE(input_resource_max_maps)]);
