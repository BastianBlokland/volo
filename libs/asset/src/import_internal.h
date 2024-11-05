#pragma once
#include "ecs_module.h"

/**
 * Global asset import environment.
 */
ecs_comp_extern(AssetImportEnvComp);

/**
 * Check if we are ready to import assets.
 * NOTE: Ready state is only valid this frame as due to hot-loading it can become un-ready.
 */
bool asset_import_ready(const AssetImportEnvComp*);
