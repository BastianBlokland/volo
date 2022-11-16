#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Prefab database.
 */

typedef enum {
  AssetPrefabTrait_Movement,
} AssetPrefabTraitType;

typedef struct {
  f32 speed;
} AssetPrefabTraitMovement;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitMovement data_movement;
  };
} AssetPrefabTrait;

typedef struct {
  StringHash nameHash;
  u16        traitIndex, traitCount; // Stored in the traits array.
} AssetPrefab;

ecs_comp_extern_public(AssetPrefabMapComp) {
  AssetPrefab*      prefabs; // Sorted on the nameHash.
  usize             prefabCount;
  AssetPrefabTrait* traits;
  usize             traitCount;
};

/**
 * Lookup a prefab by the hash of its name.
 */
const AssetPrefab* asset_prefab_get(const AssetPrefabMapComp*, StringHash nameHash);
