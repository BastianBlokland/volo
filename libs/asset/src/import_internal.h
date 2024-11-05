#pragma once
#include "ecs_module.h"

/**
 * Global asset import environment.
 */
ecs_comp_extern(AssetImportEnvComp);

/**
 * Check if we are ready to import assets.
 */
bool asset_import_ready(const AssetImportEnvComp*);
