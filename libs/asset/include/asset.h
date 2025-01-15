#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the asset library.
 */

ecs_comp_extern(AssetAtlasComp);
ecs_comp_extern(AssetComp);
ecs_comp_extern(AssetFailedComp);
ecs_comp_extern(AssetLoadedComp);
ecs_comp_extern(AssetManagerComp);
ecs_comp_extern(AssetPrefabMapComp);

typedef enum eAssetGraphicPass       AssetGraphicPass;
typedef struct sAssetGraphicOverride AssetGraphicOverride;
typedef struct sAssetLevelRef        AssetLevelRef;
typedef struct sAssetProduct         AssetProduct;
typedef struct sAssetProperty        AssetProperty;
typedef struct sAssetRef             AssetRef;
