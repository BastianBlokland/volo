#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Prefab database.
 */

typedef enum {
  AssetPrefabCollision_Sphere,
  AssetPrefabCollision_Capsule,
  AssetPrefabCollision_Box,
} AssetPrefabCollisionType;

typedef struct {
  GeoVector offset;
  f32       radius;
} AssetPrefabCollisionSphere;

typedef struct {
  GeoVector offset;
  f32       radius, height;
} AssetPrefabCollisionCapsule;

typedef struct {
  GeoVector min, max;
} AssetPrefabCollisionBox;

typedef enum {
  AssetPrefabTrait_Renderable,
  AssetPrefabTrait_Scale,
  AssetPrefabTrait_Movement,
  AssetPrefabTrait_Health,
  AssetPrefabTrait_Attack,
  AssetPrefabTrait_Collision,
  AssetPrefabTrait_Brain,
} AssetPrefabTraitType;

typedef struct {
  EcsEntityId graphic;
} AssetPrefabTraitRenderable;

typedef struct {
  f32 scale;
} AssetPrefabTraitScale;

typedef struct {
  f32 speed, radius;
} AssetPrefabTraitMovement;

typedef struct {
  f32 amount;
} AssetPrefabTraitHealth;

typedef struct {
  StringHash weapon;
} AssetPrefabTraitAttack;

typedef struct {
  AssetPrefabCollisionType type;
  bool                     navBlocker;
  union {
    AssetPrefabCollisionSphere  data_sphere;
    AssetPrefabCollisionCapsule data_capsule;
    AssetPrefabCollisionBox     data_box;
  };
} AssetPrefabTraitCollision;

typedef struct {
  EcsEntityId behavior;
} AssetPrefabTraitBrain;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitRenderable data_renderable;
    AssetPrefabTraitScale      data_scale;
    AssetPrefabTraitMovement   data_movement;
    AssetPrefabTraitHealth     data_health;
    AssetPrefabTraitAttack     data_attack;
    AssetPrefabTraitCollision  data_collision;
    AssetPrefabTraitBrain      data_brain;
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
