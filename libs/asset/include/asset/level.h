#pragma once
#include "asset/forward.h"
#include "asset/property.h"
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"
#include "geo/quat.h"

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
  u32               id; // Persistent object id.
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
  HeapArray_t(AssetProperty) properties; // NOTE: Asset properties are not automatically resolved.
  HeapArray_t(AssetLevelObject) objects; // Sorted on persistent id.
} AssetLevel;

ecs_comp_extern_public(AssetLevelComp) { AssetLevel level; };

extern DataMeta g_assetLevelDefMeta;

/**
 * Find all asset references in the given level.
 */
u32 asset_level_refs(
    const AssetLevelComp*, EcsWorld* world, AssetManagerComp*, EcsEntityId out[], u32 outMax);

const AssetLevelObject* asset_level_find(const AssetLevel*, u32 persistentId);
u32                     asset_level_find_index(const AssetLevel*, u32 persistentId);

bool asset_level_save(AssetManagerComp*, String id, const AssetLevel*);
