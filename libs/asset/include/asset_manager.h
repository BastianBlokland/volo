#pragma once
#include "core_string.h"
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

#define asset_query_max_results 512

typedef struct {
  String id, data;
} AssetMemRecord;

typedef enum {
  AssetManagerFlags_None         = 0,
  AssetManagerFlags_TrackChanges = 1 << 0,
  AssetManagerFlags_DelayUnload  = 1 << 1,
} AssetManagerFlags;

/**
 * The AssetManager is responsible for loading and unloaded assets.
 */
ecs_comp_extern(AssetManagerComp);

/**
 * Every asset has a 'AssetComp' and assets that are currently loaded have a 'AssetLoadedComp'.
 * The asset payload can be retrieved from type-specific components, for example 'AssetTextureComp'.
 */
ecs_comp_extern(AssetComp);
ecs_comp_extern(AssetFailedComp);
ecs_comp_extern(AssetLoadedComp);
ecs_comp_extern(AssetChangedComp);
ecs_comp_extern(AssetDirtyComp);

/**
 * Retrieve the identifier for the given asset.
 */
String asset_id(const AssetComp*);

/**
 * Retrieve the absolute path for the given asset.
 * NOTE: Returns false if the manager cannot retrieve the path.
 */
bool asset_path(const AssetManagerComp*, const AssetComp*, DynString* out);
bool asset_path_by_id(const AssetManagerComp*, String id, DynString* out);

/**
 * Create a asset-manager (on the global entity) that loads assets from the file-system.
 * Assets are loaded from '{rootPath}/{assetId}'.
 */
AssetManagerComp* asset_manager_create_fs(EcsWorld*, AssetManagerFlags, String rootPath);

/**
 * Create a asset-manager (on the global entity) that loads assets from a set of pre-loaded
 * in-memory sources.
 * For example useful for unit-testing.
 */
AssetManagerComp* asset_manager_create_mem(
    EcsWorld*, AssetManagerFlags, const AssetMemRecord* records, usize recordCount);

/**
 * Lookup a asset-entity by its id.
 * NOTE: The asset won't be loaded until 'asset_acquire()' is called.
 * Pre-condition: !string_is_empty(id).
 */
EcsEntityId asset_lookup(EcsWorld*, AssetManagerComp*, String id);
EcsEntityId asset_maybe_lookup(EcsWorld*, AssetManagerComp*, String id);

/**
 * Acquire the asset, this tells the manager that the asset should be loaded.
 * NOTE: The acquire takes effect in the next frame.
 * NOTE: Should be explicitly released using 'asset_release()'.
 */
void asset_acquire(EcsWorld*, EcsEntityId assetEntity);

/**
 * Release the asset, this tells the manager that you no longer need the asset and can be unloaded.
 * Pre-condition: Previously acquired using 'asset_acquire()'.
 */
void asset_release(EcsWorld*, EcsEntityId assetEntity);

/**
 * Request the given asset to be reloaded.
 * NOTE: Unload is delayed until all systems release the asset.
 */
void asset_reload_request(EcsWorld*, EcsEntityId assetEntity);

/**
 * Save an asset to the active asset repository.
 * NOTE: Returns true if the save succeeded, otherwise false.
 * Pre-condition: !string_is_empty(id).
 * Pre-condition: path_extension(id).size != 0.
 */
bool asset_save(AssetManagerComp*, String id, String data);

/**
 * Query for assets that match the given id pattern.
 *
 * Supported pattern syntax:
 * '?' matches any single character.
 * '*' matches any number of any characters including none.
 *
 * NOTE: Returns the number of found assets.
 */
u32 asset_query(
    EcsWorld*,
    AssetManagerComp*,
    String      pattern,
    EcsEntityId out[PARAM_ARRAY_SIZE(asset_query_max_results)]);

/**
 * Debug apis.
 */
u32  asset_ref_count(const AssetComp*);
u32  asset_load_count(const AssetComp*);
bool asset_is_loading(const AssetComp*);
u32  asset_ticks_until_unload(const AssetComp*);
