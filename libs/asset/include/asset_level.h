#pragma once
#include "core_sentinel.h"
#include "ecs_module.h"
#include "geo_quat.h"

typedef enum {
  AssetLevelFaction_A,
  AssetLevelFaction_B,
  AssetLevelFaction_C,
  AssetLevelFaction_D,

  AssetLevelFaction_None = sentinel_u32,
} AssetLevelFaction;

typedef struct {
  StringHash        prefabId;
  AssetLevelFaction faction;
  GeoVector         position;
  GeoQuat           rotation;
} AssetLevelEntry;

ecs_comp_extern_public(AssetLevelComp) {
  AssetLevelEntry* entries;
  u32              entryCount;
};
