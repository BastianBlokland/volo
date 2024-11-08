#include "asset_manager.h"
#include "data_registry.h"

#include "format_internal.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

// Forward declare from 'repo_internal.h'.
typedef struct sAssetSource AssetSource;

// Forward declare from 'import_internal.h'.
typedef struct sAssetImportEnvComp AssetImportEnvComp;

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
AssetSource* asset_source_open(const AssetManagerComp*, const AssetImportEnvComp*, String id);

/**
 * Watch an asset for changes, 'AssetChangedComp' will be added once a change is detected.
 * Pre-condition: !string_is_empty(id).
 */
EcsEntityId asset_watch(EcsWorld*, AssetManagerComp*, String id);

/**
 * Register an external load for the given asset.
 * Useful when loading files outside of the normal loaders.
 */
void asset_mark_external_load(EcsWorld*, EcsEntityId asset, AssetFormat format, TimeReal modTime);

/**
 * Queue data to be cached for the given asset.
 * The cached data will be used for the next load provided the source asset hasn't changed.
 */
void asset_cache(EcsWorld*, EcsEntityId asset, DataMeta, Mem data);
