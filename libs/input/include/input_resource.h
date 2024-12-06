#pragma once
#include "ecs_module.h"

/**
 * Global input resource.
 */
ecs_comp_extern(InputResourceComp);

InputResourceComp* input_resource_init(EcsWorld*);
void               input_resource_load_map(InputResourceComp*, String inputMapId);
