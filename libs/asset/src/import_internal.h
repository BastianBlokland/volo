#pragma once
#include "ecs_module.h"

/**
 * Global asset import environment.
 */
ecs_comp_extern(AssetImportEnvComp);

/**
 * Check if we are ready to import an asset with the given id.
 */
bool asset_import_ready(const AssetImportEnvComp*, String assetId);

/**
 * Lookup the import hash of an asset with the given id. When the import hash changes the asset has
 * to be re-imported.
 * Pre-condition: asset_import_ready().
 */
u32 asset_import_hash(const AssetImportEnvComp*, String assetId);
