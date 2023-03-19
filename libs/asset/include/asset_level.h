#pragma once
#include "core_sentinel.h"
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'asset_manager.h'.
ecs_comp_extern(AssetManagerComp);

typedef enum {
  AssetLevelFaction_A,
  AssetLevelFaction_B,
  AssetLevelFaction_C,
  AssetLevelFaction_D,

  AssetLevelFaction_Count,
  AssetLevelFaction_None = sentinel_u32,
} AssetLevelFaction;

typedef struct {
  u32               id; // Optional unique persistent object id.
  String            prefab;
  AssetLevelFaction faction;
  f32               scale;
  GeoVector         position;
  GeoVector         rotation; // xyz: Euler angles in degrees.
} AssetLevelObject;

typedef struct {
  const AssetLevelObject* values;
  usize                   count;
} AssetLevelObjectArray;

typedef struct {
  AssetLevelObjectArray objects;
} AssetLevel;

ecs_comp_extern_public(AssetLevelComp) { AssetLevel level; };

bool asset_level_save(AssetManagerComp*, String id, AssetLevel);
