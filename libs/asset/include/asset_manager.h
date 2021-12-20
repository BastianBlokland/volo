#pragma once
#include "core_string.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef struct {
  String id, data;
} AssetMemRecord;

/**
 * The AssetManager is responsible for loading and unloaded assets.
 */
ecs_comp_extern(AssetManagerComp);

/**
 * Every asset has a 'AssetComp' and assets that are currently loaded have a 'AssetLoadedComp'.
 * The asset payload can be retrieved from type specific components, for example 'AssetTextureComp'.
 */
ecs_comp_extern(AssetComp);
ecs_comp_extern(AssetFailedComp);
ecs_comp_extern(AssetLoadedComp);

/**
 * Create a asset-manager that loads assets from the file-system.
 * Assets are loaded from '{rootPath}/{assetId}'.
 */
EcsEntityId asset_manager_create_fs(EcsWorld*, String rootPath);

/**
 * Create a asset-manager that loads assets from a set of pre-loaded in-memory sources.
 * For example usefull for unit-testing.
 */
EcsEntityId asset_manager_create_mem(EcsWorld*, const AssetMemRecord* records, usize recordCount);

/**
 * Lookup a asset-entity by its id.
 * NOTE: The asset won't be loaded until 'asset_acquire()' is called.
 */
EcsEntityId asset_lookup(EcsWorld*, AssetManagerComp*, String id);

/**
 * Acquire the asset, this tells the manager that the asset should be loaded.
 * NOTE: Should be explicitly released using 'asset_release()'.
 */
void asset_acquire(EcsWorld*, EcsEntityId assetEntity);

/**
 * Release the asset, this tells the manager that you no longer need the asset and can be unloaded.
 * Pre-condition: Previously acquired using 'asset_acquire()'.
 */
void asset_release(EcsWorld*, EcsEntityId assetEntity);
