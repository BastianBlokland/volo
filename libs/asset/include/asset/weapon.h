#pragma once
#include "asset/ref.h"
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"

/**
 * Weapon database.
 */

typedef enum {
  AssetWeaponEffect_Projectile,
  AssetWeaponEffect_Damage,
  AssetWeaponEffect_Animation,
  AssetWeaponEffect_Vfx,
  AssetWeaponEffect_Sound,
} AssetWeaponEffectType;

typedef struct {
  StringHash   originJoint;
  bool         launchTowardsTarget, seekTowardsTarget;
  u32          applyStatus; // Mask of status-effects to apply on hit.
  f32          spreadAngle;
  f32          speed;
  f32          damage, damageRadius;
  TimeDuration delay, destroyDelay;
  StringHash   projectilePrefab;
  StringHash   impactPrefab; // Optional, 0 if unused.
} AssetWeaponEffectProj;

typedef struct {
  bool         continuous;
  StringHash   originJoint;
  f32          radius, radiusEnd;
  f32          length;
  f32          damage;
  u32          applyStatus; // Mask of status-effects to apply.
  TimeDuration lengthGrowTime;
  TimeDuration delay;
  StringHash   impactPrefab; // Optional, 0 if unused.
} AssetWeaponEffectDmg;

typedef struct {
  bool         continuous, allowEarlyInterrupt;
  StringHash   layer;
  f32          speed;
  TimeDuration delay;
} AssetWeaponEffectAnim;

typedef struct {
  StringHash   originJoint;
  f32          scale;
  bool         waitUntilFinished;
  TimeDuration delay, duration;
  AssetRef     asset;
} AssetWeaponEffectVfx;

typedef struct {
  StringHash   originJoint;
  TimeDuration delay, duration;
  AssetRef     asset;
  f32          gainMin, gainMax;
  f32          pitchMin, pitchMax;
} AssetWeaponEffectSound;

typedef struct {
  AssetWeaponEffectType type;
  union {
    AssetWeaponEffectProj  data_proj;
    AssetWeaponEffectDmg   data_dmg;
    AssetWeaponEffectAnim  data_anim;
    AssetWeaponEffectVfx   data_vfx;
    AssetWeaponEffectSound data_sound;
  };
} AssetWeaponEffect;

typedef enum {
  AssetWeapon_PredictiveAim = 1 << 0,
} AssetWeaponFlags;

typedef struct {
  StringHash       name;
  AssetWeaponFlags flags;
  u16              effectIndex, effectCount; // Stored in the effects array.
  f32              readySpeed; // Speed to increase the ready amount, when reaches 1.0 we can fire.
  bool             readyWhileMoving;
  StringHash       readyAnim;
  TimeDuration     readyMinTime; // Time to keep the weapon ready after the last shot.
  TimeDuration     intervalMin, intervalMax;
} AssetWeapon;

ecs_comp_extern_public(AssetWeaponMapComp) {
  HeapArray_t(AssetWeapon) weapons; // Sorted on the nameHash.
  HeapArray_t(AssetWeaponEffect) effects;
};

extern DataMeta g_assetWeaponDefMeta;

/**
 * Find all asset references in the given weapon map.
 */
u32 asset_weapon_refs(const AssetWeaponMapComp*, EcsEntityId out[], u32 outMax);

/**
 * Lookup weapon statistics.
 */
f32 asset_weapon_damage(const AssetWeaponMapComp*, const AssetWeapon*);
u8  asset_weapon_applies_status(const AssetWeaponMapComp*, const AssetWeapon*);

/**
 * Lookup a weapon by the hash of its name.
 */
const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp*, StringHash nameHash);
