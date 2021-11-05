#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * TODO:.
 */
ecs_comp_extern(AssetManagerComp);

/**
 * TODO:.
 */
ecs_comp_extern(AssetComp);

/**
 * TODO:.
 */
EcsEntityId asset_manager_create_fs(EcsWorld*, String rootPath);

/**
 * TODO:.
 */
EcsEntityId asset_manager_lookup(AssetManagerComp*, String id);
