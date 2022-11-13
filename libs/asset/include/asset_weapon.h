#pragma once
#include "core_time.h"
#include "ecs_module.h"

/**
 * Weapon Map.
 */

typedef struct {
  StringHash   nameHash;
  TimeDuration intervalMin, intervalMax;
  f32          aimSpeed;   // Speed to increase the aim amount, when aim reaches 1.0 we can fire.
  f32          aimMinTime; // Time to keep aiming after the last shot.
  StringHash   aimAnim;
} AssetWeapon;

ecs_comp_extern_public(AssetWeaponMapComp) {
  AssetWeapon* weapons; // Sorted on the nameHash.
  usize        weaponCount;
};

/**
 * Lookup a weapon by the hash of its name.
 */
const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp*, StringHash nameHash);
