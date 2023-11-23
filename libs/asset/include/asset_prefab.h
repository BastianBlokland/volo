#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

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
  AssetPrefabTrait_SetMember,
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
  AssetPrefabTrait_Taunt,
  AssetPrefabTrait_Location,
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
  StringHash sets[8];
} AssetPrefabTraitSetMember;

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
  u16         knowledgeIndex, knowledgeCount; // Stored in the values array.
} AssetPrefabTraitScript;

typedef struct {
  i32        priority;
  StringHash tauntDeathPrefab;   // Optional: 0 to disable.
  StringHash tauntConfirmPrefab; // Optional: 0 to disable.
} AssetPrefabTraitTaunt;

typedef struct {
  AssetPrefabShapeBox aimTarget;
} AssetPrefabTraitLocation;

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
    AssetPrefabTraitSetMember  data_setMember;
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
    AssetPrefabTraitTaunt      data_taunt;
    AssetPrefabTraitLocation   data_location;
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

typedef enum {
  AssetPrefabValue_Number,
  AssetPrefabValue_Bool,
} AssetPrefabValueType;

typedef struct {
  StringHash           name;
  AssetPrefabValueType type;
  union {
    f64  data_number;
    bool data_bool;
  };
} AssetPrefabValue;

ecs_comp_extern_public(AssetPrefabMapComp) {
  AssetPrefab*      prefabs;         // AssetPrefab[prefabCount]. Sorted on the nameHash.
  u16*              userIndexLookup; // u16[prefabCount], lookup from user-index to prefab-index.
  usize             prefabCount;
  AssetPrefabTrait* traits; // AssetPrefabTrait[traitCount];
  usize             traitCount;
  AssetPrefabValue* values;
  usize             valueCount;
};

const AssetPrefab* asset_prefab_get(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_get_index_from_user(const AssetPrefabMapComp*, u16 userIndex);

const AssetPrefabTrait*
asset_prefab_trait_get(const AssetPrefabMapComp*, const AssetPrefab*, AssetPrefabTraitType);

void asset_prefab_jsonschema_write(DynString*);
