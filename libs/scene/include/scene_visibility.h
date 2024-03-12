#pragma once
#include "ecs_module.h"
#include "geo_vector.h"
#include "scene_faction.h"

typedef struct {
  bool renderAll; // Also render invisible entities, useful for debugging.
} SceneVisibilitySettings;

ecs_comp_extern(SceneVisibilityEnvComp);
ecs_comp_extern_public(SceneVisionComp) { f32 radius; };
ecs_comp_extern_public(SceneVisibilityComp) { u8 visibleToFactionsMask; };

const SceneVisibilitySettings* scene_visibility_settings(const SceneVisibilityEnvComp*);
SceneVisibilitySettings*       scene_visibility_settings_mut(SceneVisibilityEnvComp*);

/**
 * Check if the specified visiblity component is visible for this faction.
 */
bool scene_visible(const SceneVisibilityComp*, SceneFaction);
bool scene_visible_for_render(
    const SceneVisibilityEnvComp*, const SceneVisibilityComp*, SceneFaction);

/**
 * Check if the specified position is visible for this faction.
 */
bool scene_visible_pos(const SceneVisibilityEnvComp*, SceneFaction, GeoVector pos);
