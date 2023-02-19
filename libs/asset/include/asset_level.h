#pragma once
#include "core_sentinel.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  AssetLevelFaction_A,
  AssetLevelFaction_B,
  AssetLevelFaction_C,
  AssetLevelFaction_D,

  AssetLevelFaction_None = sentinel_u32,
} AssetLevelFaction;

typedef struct {
  String            prefab;
  AssetLevelFaction faction;
  GeoVector         position;
  GeoVector         rotation; // xyz: Euler angles in degrees.
} AssetLevelObject;

ecs_comp_extern_public(AssetLevelComp) {
  struct {
    AssetLevelObject* values;
    usize             count;
  } objects;
};
