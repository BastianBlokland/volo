#pragma once
#include "asset.h"
#include "core_array.h"
#include "core_sentinel.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_quat.h"

typedef enum {
  AssetLevelFaction_None,
  AssetLevelFaction_A,
  AssetLevelFaction_B,
  AssetLevelFaction_C,
  AssetLevelFaction_D
} AssetLevelFaction;

typedef enum {
  AssetLevelFog_Disabled,
  AssetLevelFog_VisibilityBased,

  AssetLevelFog_Count,
} AssetLevelFog;

typedef struct {
  u32               id; // Optional unique persistent object id.
  StringHash        prefab;
  AssetLevelFaction faction;
  f32               scale;
  GeoVector         position;
  GeoQuat           rotation;
} AssetLevelObject;

typedef struct {
  String        name;
  String        terrainId;
  AssetLevelFog fogMode;
  GeoVector     startpoint;
  HeapArray_t(AssetLevelObject) objects;
} AssetLevel;

ecs_comp_extern_public(AssetLevelComp) { AssetLevel level; };

extern DataMeta g_assetLevelDefMeta;

bool asset_level_save(AssetManagerComp*, String id, const AssetLevel*);
