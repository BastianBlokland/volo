#pragma once
#include "core_sentinel.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_quat.h"

// Forward declare from 'asset_manager.h'.
ecs_comp_extern(AssetManagerComp);

typedef enum {
  AssetLevelFaction_None,
  AssetLevelFaction_A,
  AssetLevelFaction_B,
  AssetLevelFaction_C,
  AssetLevelFaction_D
} AssetLevelFaction;

typedef struct {
  u32               id; // Optional unique persistent object id.
  String            prefab;
  AssetLevelFaction faction;
  f32               scale;
  GeoVector         position;
  GeoQuat           rotation;
} AssetLevelObject;

typedef struct {
  const AssetLevelObject* values;
  usize                   count;
} AssetLevelObjectArray;

typedef struct {
  String                name;
  String                terrainId;
  GeoVector             startpoint;
  AssetLevelObjectArray objects;
} AssetLevel;

ecs_comp_extern_public(AssetLevelComp) { AssetLevel level; };

extern DataMeta g_assetLevelDefMeta;

bool asset_level_save(AssetManagerComp*, String id, const AssetLevel*);
