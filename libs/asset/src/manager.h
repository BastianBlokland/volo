#include "asset/manager.h"
#include "data/registry.h"

#include "format.h"
#include "forward.h"

/**
 * Register a dependency between the two assets.
 * When 'dependency' is changed the 'asset' is also marked as changed.
 * NOTE: At the moment its not possible to unregister a dependency.
 */
void asset_register_dep(EcsWorld*, EcsEntityId asset, EcsEntityId dependency);

/**
 * Synchonously open an asset.
 * NOTE: Does not register the asset with the manager.
 */
bool asset_source_stat(
    const AssetManagerComp*, const AssetImportEnvComp*, String id, AssetInfo* out);

/**
 * Synchonously open an asset.
 * NOTE: Does not register the asset with the manager and does not trigger loaders.
 */
AssetSource* asset_source_open(const AssetManagerComp*, const AssetImportEnvComp*, String id);

/**
 * Watch an asset for changes, 'AssetChangedComp' will be added once a change is detected.
 * Pre-condition: !string_is_empty(id).
 */
EcsEntityId asset_watch(EcsWorld*, AssetManagerComp*, String id);

/**
 * Mark the completion of an asset load.
 * Pre-condition: Asset is currently loading.
 */
void asset_mark_load_failure(EcsWorld*, EcsEntityId asset, String id, String error, i32 errorCode);
void asset_mark_load_success(EcsWorld*, EcsEntityId asset);

/**
 * Register an external load for the given asset.
 * Useful when loading files outside of the normal loaders.
 */
void asset_mark_external_load(
    EcsWorld*, EcsEntityId asset, AssetFormat, u32 checksum /* crc32 */, TimeReal modTime);

/**
 * Queue data to be cached for the given asset.
 * The cached data will be used for the next load provided the source asset hasn't changed.
 */
void asset_cache(EcsWorld*, EcsEntityId asset, DataMeta, Mem data);
