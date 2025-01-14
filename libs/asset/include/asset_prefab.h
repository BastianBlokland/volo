#pragma once
#include "asset_ref.h"
#include "core_array.h"
#include "core_time.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_color.h"
#include "geo_sphere.h"

/**
 * Prefab database.
 */

#define asset_prefab_scripts_max 7
#define asset_prefab_sets_max 8
#define asset_prefab_sounds_max 4

typedef enum {
  AssetPrefabTrait_Name,
  AssetPrefabTrait_SetMember,
  AssetPrefabTrait_Renderable,
  AssetPrefabTrait_Vfx,
  AssetPrefabTrait_Decal,
  AssetPrefabTrait_Sound,
  AssetPrefabTrait_LightPoint,
  AssetPrefabTrait_LightDir,
  AssetPrefabTrait_LightAmbient,
  AssetPrefabTrait_Lifetime,
  AssetPrefabTrait_Movement,
  AssetPrefabTrait_Footstep,
  AssetPrefabTrait_Health,
  AssetPrefabTrait_Attack,
  AssetPrefabTrait_Collision,
  AssetPrefabTrait_Script,
  AssetPrefabTrait_Bark,
  AssetPrefabTrait_Location,
  AssetPrefabTrait_Status,
  AssetPrefabTrait_Vision,
  AssetPrefabTrait_Attachment,
  AssetPrefabTrait_Production,
  AssetPrefabTrait_Scalable,

  AssetPrefabTrait_Count,
} AssetPrefabTraitType;

typedef struct {
  StringHash name;
} AssetPrefabTraitName;

typedef struct {
  StringHash sets[asset_prefab_sets_max];
} AssetPrefabTraitSetMember;

typedef struct {
  AssetRef graphic;
} AssetPrefabTraitRenderable;

typedef struct {
  AssetRef asset;
} AssetPrefabTraitVfx;

typedef struct {
  AssetRef asset;
} AssetPrefabTraitDecal;

typedef struct {
  AssetRef assets[asset_prefab_sounds_max]; // Random asset will be selected when spawned.
  f32      gainMin, gainMax;
  f32      pitchMin, pitchMax;
  bool     looping;
  bool     persistent; // Pre-load the asset and keep it in memory.
} AssetPrefabTraitSound;

typedef struct {
  GeoColor radiance;
  f32      radius;
} AssetPrefabTraitLightPoint;

typedef struct {
  GeoColor radiance;
  bool     shadows, coverage;
} AssetPrefabTraitLightDir;

typedef struct {
  f32 intensity;
} AssetPrefabTraitLightAmbient;

typedef struct {
  TimeDuration duration;
} AssetPrefabTraitLifetime;

typedef struct {
  f32        speed;
  f32        rotationSpeed; // Radians per second.
  f32        radius, weight;
  StringHash moveAnimation; // Optional: 0 to disable.
  u32        navLayer;
  bool       wheeled;
  f32        wheeledAcceleration;
} AssetPrefabTraitMovement;

typedef struct {
  StringHash jointA, jointB;
  AssetRef   decalA, decalB;
} AssetPrefabTraitFootstep;

typedef struct {
  f32          amount;
  TimeDuration deathDestroyDelay;
  StringHash   deathEffectPrefab; // Optional: 0 to disable.
} AssetPrefabTraitHealth;

typedef struct {
  StringHash weapon;
  StringHash aimJoint;
  f32        aimSpeed; // Radians per second.
  f32        targetRangeMin, targetRangeMax;
  bool       targetExcludeUnreachable;
  bool       targetExcludeObscured;
} AssetPrefabTraitAttack;

typedef struct {
  bool navBlocker;
  u16  shapeIndex, shapeCount; // Stored in the shapes array.
} AssetPrefabTraitCollision;

typedef struct {
  EcsEntityId scripts[asset_prefab_scripts_max];
  u16         propIndex, propCount; // Stored in the values array.
} AssetPrefabTraitScript;

typedef struct {
  i32        priority;
  StringHash barkDeathPrefab;   // Optional: 0 to disable.
  StringHash barkConfirmPrefab; // Optional: 0 to disable.
} AssetPrefabTraitBark;

typedef struct {
  GeoBox aimTarget;
} AssetPrefabTraitLocation;

typedef struct {
  u32        supportedStatus; // Mask of status-effects that can be applied to this entity.
  StringHash effectJoint;
} AssetPrefabTraitStatus;

typedef struct {
  f32  radius;
  bool showInHud;
} AssetPrefabTraitVision;

typedef struct {
  StringHash attachmentPrefab;
  f32        attachmentScale;
  StringHash joint;
  GeoVector  offset;
} AssetPrefabTraitAttachment;

typedef struct {
  GeoVector  spawnPos, rallyPos;
  AssetRef   rallySound;
  f32        rallySoundGain;
  StringHash productSetId;
  f32        placementRadius;
} AssetPrefabTraitProduction;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitName         data_name;
    AssetPrefabTraitSetMember    data_setMember;
    AssetPrefabTraitRenderable   data_renderable;
    AssetPrefabTraitVfx          data_vfx;
    AssetPrefabTraitDecal        data_decal;
    AssetPrefabTraitSound        data_sound;
    AssetPrefabTraitLightPoint   data_lightPoint;
    AssetPrefabTraitLightDir     data_lightDir;
    AssetPrefabTraitLightAmbient data_lightAmbient;
    AssetPrefabTraitLifetime     data_lifetime;
    AssetPrefabTraitMovement     data_movement;
    AssetPrefabTraitFootstep     data_footstep;
    AssetPrefabTraitHealth       data_health;
    AssetPrefabTraitAttack       data_attack;
    AssetPrefabTraitCollision    data_collision;
    AssetPrefabTraitScript       data_script;
    AssetPrefabTraitBark         data_bark;
    AssetPrefabTraitLocation     data_location;
    AssetPrefabTraitStatus       data_status;
    AssetPrefabTraitVision       data_vision;
    AssetPrefabTraitAttachment   data_attachment;
    AssetPrefabTraitProduction   data_production;
  };
} AssetPrefabTrait;

/**
 * Sanity check that we are not making the trait's very big.
 * NOTE: This is not a hard limit but when making this bigger consider changing this to SOA storage.
 */
ASSERT(sizeof(AssetPrefabTrait) <= 128, "AssetPrefabTrait too big");

typedef enum {
  AssetPrefabFlags_Infantry     = 1 << 0,
  AssetPrefabFlags_Vehicle      = 1 << 1,
  AssetPrefabFlags_Structure    = 1 << 2,
  AssetPrefabFlags_Destructible = 1 << 3,
  AssetPrefabFlags_Volatile     = 1 << 4, // Prefab should not be persisted.

  AssetPrefabFlags_Unit =
      AssetPrefabFlags_Infantry | AssetPrefabFlags_Vehicle | AssetPrefabFlags_Structure
} AssetPrefabFlags;

typedef struct {
  StringHash       name;
  u32              hash; // Hash of prefab content. NOTE: Non deterministic across sessions.
  AssetPrefabFlags flags;
  u16              traitIndex, traitCount; // Stored in the traits array.
} AssetPrefab;

typedef enum {
  AssetPrefabValue_Number,
  AssetPrefabValue_Bool,
  AssetPrefabValue_Vector3,
  AssetPrefabValue_Color,
  AssetPrefabValue_String,
  AssetPrefabValue_Asset,
  AssetPrefabValue_Sound,
} AssetPrefabValueType;

typedef struct {
  AssetRef asset;
  bool     persistent; // Pre-load the asset and keep it in memory.
} AssetPrefabValueSound;

typedef struct {
  StringHash           name;
  AssetPrefabValueType type;
  union {
    f64                   data_number;
    bool                  data_bool;
    GeoVector             data_vector3;
    GeoColor              data_color;
    StringHash            data_string;
    AssetRef              data_asset;
    AssetPrefabValueSound data_sound;
  };
} AssetPrefabValue;

typedef enum {
  AssetPrefabShape_Sphere,
  AssetPrefabShape_Capsule,
  AssetPrefabShape_Box,
} AssetPrefabShapeType;

typedef struct {
  AssetPrefabShapeType type;
  union {
    GeoSphere     data_sphere;
    GeoCapsule    data_capsule;
    GeoBoxRotated data_box;
  };
} AssetPrefabShape;

ecs_comp_extern_public(AssetPrefabMapComp) {
  AssetPrefab* prefabs; // AssetPrefab[prefabCount]. Sorted on the nameHash.
  usize        prefabCount;
  String*      userNames;  // String[prefabCount]. Interned, NOTE: In user-index order.
  u16*         userLookup; // u16[prefabCount * 2], Lookups from prefab <-> user indices.
  HeapArray_t(AssetPrefabTrait) traits;
  HeapArray_t(AssetPrefabValue) values;
  HeapArray_t(AssetPrefabShape) shapes;
  HeapArray_t(AssetRef) persistentSounds;
};

extern DataMeta g_assetPrefabDefMeta;

const AssetPrefab* asset_prefab_find(const AssetPrefabMapComp*, StringHash nameHash);
u16                asset_prefab_find_index(const AssetPrefabMapComp*, StringHash nameHash);

u16 asset_prefab_index_to_user(const AssetPrefabMapComp*, u16 prefabIndex);
u16 asset_prefab_index_from_user(const AssetPrefabMapComp*, u16 userIndex);

const AssetPrefabTrait*
asset_prefab_trait(const AssetPrefabMapComp*, const AssetPrefab*, AssetPrefabTraitType);
