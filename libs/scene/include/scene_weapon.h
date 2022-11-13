#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Global weapon resource.
 */
ecs_comp_extern(SceneWeaponResourceComp);

/**
 * Create a new weapon resource from the given WeaponMap.
 */
void scene_weapon_init(EcsWorld*, String weaponMapId);

/**
 * Retrieve the asset entity of the global weapon map.
 */
EcsEntityId scene_weapon_map(const SceneWeaponResourceComp*);
