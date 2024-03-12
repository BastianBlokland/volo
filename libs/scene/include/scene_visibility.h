#pragma once
#include "ecs_module.h"
#include "geo_vector.h"
#include "scene_faction.h"

typedef enum {
  SceneVisibilityFlags_ForceVisibleForRender = 1 << 0, // Disable visibility checks for rendering.
} SceneVisibilityFlags;

ecs_comp_extern(SceneVisibilityEnvComp);
ecs_comp_extern_public(SceneVisionComp) { f32 radius; };
ecs_comp_extern_public(SceneVisibilityComp) { u8 visibleToFactionsMask; };

SceneVisibilityFlags scene_visibility_flags(const SceneVisibilityEnvComp*);
void                 scene_visibility_flags_set(SceneVisibilityEnvComp*, SceneVisibilityFlags);
void                 scene_visibility_flags_clear(SceneVisibilityEnvComp*, SceneVisibilityFlags);

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
