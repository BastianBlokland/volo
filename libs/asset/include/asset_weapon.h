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
  TimeDuration delay, lifetime;
  f32          spreadAngle;
  f32          speed;
  f32          damage;
  EcsEntityId  vfxProjectile, vfxImpact;
} AssetWeaponEffectProj;

typedef struct {
  StringHash   originJoint;
  TimeDuration delay;
  f32          radius;
  f32          damage;
} AssetWeaponEffectDmg;

typedef struct {
  StringHash   layer;
  TimeDuration delay;
  f32          speed;
} AssetWeaponEffectAnim;

typedef struct {
  StringHash   originJoint;
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

typedef struct {
  StringHash   nameHash;
  TimeDuration intervalMin, intervalMax;
  f32          aimSpeed;   // Speed to increase the aim amount, when aim reaches 1.0 we can fire.
  TimeDuration aimMinTime; // Time to keep aiming after the last shot.
  StringHash   aimAnim;
  u16          effectIndex, effectCount; // Stored in the effects array.
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
