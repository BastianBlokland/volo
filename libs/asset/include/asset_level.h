#pragma once
#include "asset.h"
#include "asset_property.h"
#include "core_array.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_quat.h"

#define asset_level_sets_max 8

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
  HeapArray_t(AssetProperty) properties; // NOTE: Asset properties are not automatically resolved.
  StringHash sets[asset_level_sets_max];
} AssetLevelObject;

typedef struct {
  String        name;
  AssetRef      terrain; // NOTE: Reference is not automatically resolved.
  AssetLevelFog fogMode;
  GeoVector     startpoint;
  HeapArray_t(AssetLevelObject) objects;
} AssetLevel;

ecs_comp_extern_public(AssetLevelComp) { AssetLevel level; };

extern DataMeta g_assetLevelDefMeta;

bool asset_level_save(AssetManagerComp*, String id, const AssetLevel*);
