#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

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
  f32          radius;
  f32          damage;
  bool         applyBurning;
  TimeDuration delay;
  StringHash   impactPrefab; // Optional, 0 if unused.
} AssetWeaponEffectDmg;

typedef struct {
  bool         continuous;
  StringHash   layer;
  f32          speed;
  TimeDuration delay;
  TimeDuration durationMax;
} AssetWeaponEffectAnim;

typedef struct {
  StringHash   originJoint;
  f32          scale;
  bool         waitUntilFinished;
  TimeDuration delay, duration;
  EcsEntityId  asset;
} AssetWeaponEffectVfx;

typedef struct {
  StringHash   originJoint;
  TimeDuration delay, duration;
  EcsEntityId  asset;
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
  StringHash       nameHash;
  AssetWeaponFlags flags;
  StringHash       attachmentPrefab;
  StringHash       attachmentJoint;
  u16              effectIndex, effectCount; // Stored in the effects array.
  f32              readySpeed; // Speed to increase the ready amount, when reaches 1.0 we can fire.
  StringHash       readyAnim;
  TimeDuration     readyMinTime; // Time to keep the weapon ready after the last shot.
  TimeDuration     intervalMin, intervalMax;
} AssetWeapon;

ecs_comp_extern_public(AssetWeaponMapComp) {
  AssetWeapon*       weapons; // Sorted on the nameHash.
  usize              weaponCount;
  AssetWeaponEffect* effects;
  usize              effectCount;
};

/**
 * Lookup a weapon by the hash of its name.
 */
const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp*, StringHash nameHash);
