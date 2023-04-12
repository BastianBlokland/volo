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
} AssetWeaponEffectType;

typedef struct {
  StringHash   originJoint;
  bool         launchTowardsTarget, seekTowardsTarget;
  f32          spreadAngle;
  f32          speed;
  f32          damage, damageRadius;
  TimeDuration delay, lifetime, destroyDelay;
  EcsEntityId  vfxProjectile;
  StringHash   impactPrefab; // Optional, 0 if unused.
} AssetWeaponEffectProj;

typedef struct {
  StringHash   originJoint;
  f32          radius;
  f32          damage;
  TimeDuration delay;
  EcsEntityId  vfxImpact; // Optional, 0 if unused.
} AssetWeaponEffectDmg;

typedef struct {
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
  AssetWeaponEffectType type;
  union {
    AssetWeaponEffectProj data_proj;
    AssetWeaponEffectDmg  data_dmg;
    AssetWeaponEffectAnim data_anim;
    AssetWeaponEffectVfx  data_vfx;
  };
} AssetWeaponEffect;

typedef enum {
  AssetWeapon_PredictiveAim = 1 << 0,
} AssetWeaponFlags;

typedef struct {
  StringHash       nameHash;
  AssetWeaponFlags flags;
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
