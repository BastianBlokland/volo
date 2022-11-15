#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global input resource.
 */
ecs_comp_extern(InputResourceComp);

/**
 * Create a new input resource from the given inputmap.
 */
void input_resource_init(EcsWorld*, String inputMapId);
