#pragma once
#include "ecs_module.h"

/**
 * Raw assets are not processed by the asset-system and contain the exact source content.
 */
ecs_comp_extern_public(AssetRawComp) { String data; };
