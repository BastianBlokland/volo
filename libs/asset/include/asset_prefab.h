#pragma once
#include "asset_data.h"
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
  AssetPrefabTrait_Name,
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
  AssetPrefabTrait_Script,
  AssetPrefabTrait_Brain,
  AssetPrefabTrait_Blink,
  AssetPrefabTrait_Taunt,
  AssetPrefabTrait_Location,
  AssetPrefabTrait_Explosive,
  AssetPrefabTrait_Status,
  AssetPrefabTrait_Vision,
  AssetPrefabTrait_Production,
  AssetPrefabTrait_Scalable,

  AssetPrefabTrait_Count,
} AssetPrefabTraitType;

typedef struct {
  StringHash name;
} AssetPrefabTraitName;

typedef struct {
  EcsEntityId graphic;
} AssetPrefabTraitRenderable;

typedef struct {
  EcsEntityId asset;
} AssetPrefabTraitVfx;

typedef struct {
  EcsEntityId asset;
} AssetPrefabTraitDecal;

typedef struct {
  EcsEntityId assets[4]; // Random asset will be selected when spawned, 0 for an empty slot.
  f32         gainMin, gainMax;
  f32         pitchMin, pitchMax;
  bool        looping;
  bool        persistent; // Pre-load the asset and keep it in memory.
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
  StringHash   deathEffectPrefab; // Optional: 0 to disable.
} AssetPrefabTraitHealth;

typedef struct {
  StringHash  weapon;
  StringHash  aimJoint;
  f32         aimSpeedRad; // Radians per second.
  EcsEntityId aimSoundAsset;
  f32         targetRangeMin, targetRangeMax;
  f32         targetLineOfSightRadius;
  bool        targetExcludeUnreachable;
  bool        targetExcludeObscured;
} AssetPrefabTraitAttack;

typedef struct {
  bool             navBlocker;
  AssetPrefabShape shape;
} AssetPrefabTraitCollision;

typedef struct {
  EcsEntityId scriptAsset;
} AssetPrefabTraitScript;

typedef struct {
  EcsEntityId behaviorAsset;
} AssetPrefabTraitBrain;

typedef struct {
  f32        frequency;
  StringHash effectPrefab; // Optional: 0 to disable.
} AssetPrefabTraitBlink;

typedef struct {
  i32        priority;
  StringHash tauntDeathPrefab;   // Optional: 0 to disable.
  StringHash tauntConfirmPrefab; // Optional: 0 to disable.
} AssetPrefabTraitTaunt;

typedef struct {
  AssetPrefabShapeBox aimTarget;
} AssetPrefabTraitLocation;

typedef struct {
  TimeDuration delay;
  f32          radius, damage;
} AssetPrefabTraitExplosive;

typedef struct {
  StringHash effectJoint;
  bool       burnable;
} AssetPrefabTraitStatus;

typedef struct {
  f32 radius;
} AssetPrefabTraitVision;

typedef struct {
  GeoVector   spawnPos, rallyPos;
  EcsEntityId rallySoundAsset;
  f32         rallySoundGain;
  StringHash  productSetId;
  f32         placementRadius;
} AssetPrefabTraitProduction;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitName       data_name;
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
    AssetPrefabTraitScript     data_script;
    AssetPrefabTraitBrain      data_brain;
    AssetPrefabTraitBlink      data_blink;
    AssetPrefabTraitTaunt      data_taunt;
    AssetPrefabTraitLocation   data_location;
    AssetPrefabTraitExplosive  data_explosive;
    AssetPrefabTraitStatus     data_status;
    AssetPrefabTraitVision     data_vision;
    AssetPrefabTraitProduction data_production;
  };
} AssetPrefabTrait;

typedef enum {
  AssetPrefabFlags_Infantry     = 1 << 0,
  AssetPrefabFlags_Structure    = 1 << 1,
  AssetPrefabFlags_Destructible = 1 << 2,
  AssetPrefabFlags_Volatile     = 1 << 3, // Prefab should not be persisted.
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

const AssetPrefab* asset_prefab_get(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index_from_user(const AssetPrefabMapComp*, u16 userIndex);

const AssetPrefabTrait*
asset_prefab_trait_get(const AssetPrefabMapComp*, const AssetPrefab*, AssetPrefabTraitType);

AssetDataReg asset_prefab_datareg(void);
