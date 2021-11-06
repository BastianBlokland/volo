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
EcsEntityId asset_lookup(EcsWorld*, AssetManagerComp*, String id);

/**
 * TODO:.
 */
void asset_acquire(EcsWorld*, AssetComp*);

/**
 * TODO:.
 */
void asset_release(EcsWorld*, AssetComp*);
