#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);

/**
 * Mark the entity as blocking navigation.
 */
ecs_comp_extern(SceneNavBlockerComp);

void scene_nav_add_blocker(EcsWorld*, EcsEntityId);

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp*);
GeoVector    scene_nav_cell_size(const SceneNavEnvComp*);

GeoVector scene_nav_position(const SceneNavEnvComp*, GeoNavCell);
GeoVector scene_nav_size(const SceneNavEnvComp*, GeoNavCell);
GeoBox    scene_nav_box(const SceneNavEnvComp*, GeoNavCell);
bool      scene_nav_blocked(const SceneNavEnvComp*, GeoNavCell);
