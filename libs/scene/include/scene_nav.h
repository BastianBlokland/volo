#pragma once
#include "ecs_module.h"
#include "geo_nav.h"

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp*);
GeoVector    scene_nav_position(const SceneNavEnvComp*, GeoNavCell);
