#pragma once
#include "ecs_module.h"
#include "geo_vector.h"
#include "scene_faction.h"

ecs_comp_extern_public(SceneVisibilityEnvComp);

ecs_comp_extern_public(SceneVisionComp) { f32 radius; };

/**
 * Check if the specified position is visible for this faction.
 */
bool scene_visible(const SceneVisibilityEnvComp*, GeoVector pos, SceneFaction);
