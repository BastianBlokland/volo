#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the asset library.
 */

ecs_comp_extern(AssetAtlasComp);
ecs_comp_extern(AssetManagerComp);
ecs_comp_extern(AssetPrefabMapComp);

typedef enum eAssetGraphicPass       AssetGraphicPass;
typedef enum eAssetPropertyType      AssetPropertyType;
typedef struct sAssetGraphicOverride AssetGraphicOverride;
typedef struct sAssetProduct         AssetProduct;
typedef struct sAssetRef             AssetRef;
typedef union uAssetProperty         AssetProperty;
