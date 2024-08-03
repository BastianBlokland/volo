#include "asset_manager.h"

// Forward declare from 'repo_internal.h'.
typedef struct sAssetSource AssetSource;

/**
 * Register a dependency between the two assets.
 * When 'dependency' is changed the 'asset' is also marked as changed.
 * NOTE: At the moment its not possible to unregister a dependency.
 */
void asset_register_dep(EcsWorld* world, EcsEntityId asset, EcsEntityId dependency);

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
