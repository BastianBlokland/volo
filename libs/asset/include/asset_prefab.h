#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Prefab database.
 */

typedef enum {
  AssetPrefabShape_Sphere,
  AssetPrefabShape_Capsule,
  AssetPrefabShape_Box,
} AssetPrefabShapeType;

typedef struct {
  GeoVector offset;
  f32       radius;
} AssetPrefabShapeSphere;

typedef struct {
  GeoVector offset;
  f32       radius, height;
} AssetPrefabShapeCapsule;

typedef struct {
  GeoVector min, max;
} AssetPrefabShapeBox;

typedef struct {
  AssetPrefabShapeType type;
  union {
    AssetPrefabShapeSphere  data_sphere;
    AssetPrefabShapeCapsule data_capsule;
    AssetPrefabShapeBox     data_box;
  };
} AssetPrefabShape;

typedef enum {
  AssetPrefabTrait_Renderable,
  AssetPrefabTrait_Vfx,
  AssetPrefabTrait_Decal,
  AssetPrefabTrait_Sound,
  AssetPrefabTrait_Lifetime,
  AssetPrefabTrait_Movement,
  AssetPrefabTrait_Footstep,
  AssetPrefabTrait_Health,
  AssetPrefabTrait_Attack,
  AssetPrefabTrait_Collision,
  AssetPrefabTrait_Brain,
  AssetPrefabTrait_Spawner,
  AssetPrefabTrait_Scalable,

  AssetPrefabTrait_Count,
} AssetPrefabTraitType;

typedef struct {
  EcsEntityId graphic;
  f32         blinkFrequency; // Optional: negative or 0 to disable.
} AssetPrefabTraitRenderable;

typedef struct {
  EcsEntityId asset;
} AssetPrefabTraitVfx;

typedef struct {
  EcsEntityId asset;
} AssetPrefabTraitDecal;

typedef struct {
  EcsEntityId asset;
  f32         gainMin, gainMax;
  f32         pitchMin, pitchMax;
} AssetPrefabTraitSound;

typedef struct {
  TimeDuration duration;
} AssetPrefabTraitLifetime;

typedef struct {
  f32        speed;
  f32        rotationSpeedRad; // Radians per second.
  f32        radius;
  StringHash moveAnimation; // Optional: 0 to disable.
} AssetPrefabTraitMovement;

typedef struct {
  StringHash  jointA, jointB;
  EcsEntityId decalAssetA, decalAssetB;
} AssetPrefabTraitFootstep;

typedef struct {
  f32          amount;
  TimeDuration deathDestroyDelay;
  EcsEntityId  deathVfx; // Optional: 0 to disable.
} AssetPrefabTraitHealth;

typedef struct {
  StringHash weapon;
  StringHash aimJoint;
  f32        aimSpeedRad; // Radians per second.
  f32        targetDistanceMin, targetDistanceMax;
  f32        targetLineOfSightRadius;
  bool       targetExcludeUnreachable;
  bool       targetExcludeObscured;
} AssetPrefabTraitAttack;

typedef struct {
  bool             navBlocker;
  AssetPrefabShape shape;
} AssetPrefabTraitCollision;

typedef struct {
  EcsEntityId behavior;
} AssetPrefabTraitBrain;

typedef struct {
  StringHash   prefabId;
  f32          radius;
  u32          count;
  u32          maxInstances;
  TimeDuration intervalMin, intervalMax;
} AssetPrefabTraitSpawner;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitRenderable data_renderable;
    AssetPrefabTraitVfx        data_vfx;
    AssetPrefabTraitDecal      data_decal;
    AssetPrefabTraitSound      data_sound;
    AssetPrefabTraitLifetime   data_lifetime;
    AssetPrefabTraitMovement   data_movement;
    AssetPrefabTraitFootstep   data_footstep;
    AssetPrefabTraitHealth     data_health;
    AssetPrefabTraitAttack     data_attack;
    AssetPrefabTraitCollision  data_collision;
    AssetPrefabTraitBrain      data_brain;
    AssetPrefabTraitSpawner    data_spawner;
  };
} AssetPrefabTrait;

typedef enum {
  AssetPrefabFlags_Unit     = 1 << 0,
  AssetPrefabFlags_Volatile = 1 << 1, // Prefab should not be persisted.
} AssetPrefabFlags;

typedef struct {
  StringHash       nameHash;
  AssetPrefabFlags flags;
  u16              traitIndex, traitCount; // Stored in the traits array.
} AssetPrefab;

ecs_comp_extern_public(AssetPrefabMapComp) {
  AssetPrefab*      prefabs;         // AssetPrefab[prefabCount]. Sorted on the nameHash.
  u16*              userIndexLookup; // u16[prefabCount], lookup from user-index to prefab-index.
  usize             prefabCount;
  AssetPrefabTrait* traits; // AssetPrefabTrait[traitCount];
  usize             traitCount;
};

/**
 * Lookup a prefab by the hash of its name.
 */
const AssetPrefab* asset_prefab_get(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index_from_user(const AssetPrefabMapComp*, u16 userIndex);
