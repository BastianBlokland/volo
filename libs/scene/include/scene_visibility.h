#pragma once
#include "ecs_module.h"
#include "geo_vector.h"
#include "scene_faction.h"

ecs_comp_extern(SceneVisibilityEnvComp);
ecs_comp_extern_public(SceneVisionComp) { f32 radius; };
ecs_comp_extern_public(SceneVisibilityComp) { u8 visibleToFactionsMask; };

/**
 * Check if the specified visiblity component is visible for this faction.
 */
bool scene_visible(const SceneVisibilityComp*, SceneFaction);

/**
 * Check if the specified position is visible for this faction.
 */
bool scene_visible_pos(const SceneVisibilityEnvComp*, SceneFaction, GeoVector pos);
