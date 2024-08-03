#include "asset_manager.h"
#include "data_registry.h"

// Forward declare from 'repo_internal.h'.
typedef struct sAssetSource AssetSource;

/**
 * Register a dependency between the two assets.
 * When 'dependency' is changed the 'asset' is also marked as changed.
 * NOTE: At the moment its not possible to unregister a dependency.
 */
void asset_register_dep(EcsWorld*, EcsEntityId asset, EcsEntityId dependency);

/**
 * Synchonously open an asset.
 * NOTE: Does not register the asset with the manager and does not trigger loaders.
 */
AssetSource* asset_source_open(const AssetManagerComp*, String id);

/**
 * Watch an asset for changes, 'AssetChangedComp' will be added once a change is detected.
 * Pre-condition: !string_is_empty(id).
 */
EcsEntityId asset_watch(EcsWorld*, AssetManagerComp*, String id);

/**
 * Queue data to be cached for the given asset.
 * The cached data will be used for the next load provided the source asset hasn't changed.
 */
void asset_cache(EcsWorld*, EcsEntityId asset, DataMeta, Mem data);
