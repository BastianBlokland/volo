#pragma once
#include "core_string.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef struct {
  String id, data;
} AssetMemRecord;

/**
 * TODO:.
 */
ecs_comp_extern(AssetManagerComp);

/**
 * TODO:.
 */
ecs_comp_extern(AssetComp);
ecs_comp_extern(AssetLoadedComp);

/**
 * TODO:.
 */
EcsEntityId asset_manager_create_fs(EcsWorld*, String rootPath);

/**
 * TODO:.
 */
EcsEntityId asset_manager_create_mem(EcsWorld*, AssetMemRecord* records, usize recordCount);

/**
 * TODO:.
 */
EcsEntityId asset_lookup(EcsWorld*, AssetManagerComp*, String id);

/**
 * TODO:.
 */
void asset_acquire(EcsWorld*, EcsEntityId assetEntity);

/**
 * TODO:.
 */
void asset_release(EcsWorld*, EcsEntityId assetEntity);
