#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global prefab resource.
 */
ecs_comp_extern(ScenePrefabResourceComp);

/**
 * Create a new prefab resource from the given PrefabMap.
 */
void scene_prefab_init(EcsWorld*, String prefabMapId);
