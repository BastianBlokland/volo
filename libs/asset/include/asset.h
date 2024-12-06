#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the asset library.
 */

ecs_comp_extern(AssetAtlasComp);
ecs_comp_extern(AssetManagerComp);

typedef enum eAssetGraphicPass       AssetGraphicPass;
typedef struct sAssetGraphicOverride AssetGraphicOverride;
typedef struct sAssetProduct         AssetProduct;
