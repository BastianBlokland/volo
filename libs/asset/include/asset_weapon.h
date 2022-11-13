#pragma once
#include "core_time.h"
#include "ecs_module.h"

/**
 * Weapon Map.
 */

typedef struct {
  StringHash   nameHash;
  TimeDuration intervalMin, intervalMax;
} AssetWeapon;

ecs_comp_extern_public(AssetWeaponMapComp) {
  AssetWeapon* weapons; // Sorted on the nameHash.
  usize        weaponCount;
};

/**
 * Lookup a weapon by the hash of its name.
 */
const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp*, StringHash nameHash);
