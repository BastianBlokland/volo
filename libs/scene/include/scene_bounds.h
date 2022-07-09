#pragma once
#include "ecs_module.h"
#include "geo_box_rotated.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);
ecs_comp_extern(SceneScaleComp);

ecs_comp_extern_public(SceneBoundsComp) { GeoBox local; };

/**
 * Compute the world-space axis-aligned box for the given bounds.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */
GeoBox scene_bounds_world(const SceneBoundsComp*, const SceneTransformComp*, const SceneScaleComp*);

/**
 * Compute the world-space rotated box for the given bounds.
 * NOTE: SceneTransformComp and SceneScaleComp are optional.
 */
GeoBoxRotated scene_bounds_world_rotated(
    const SceneBoundsComp*, const SceneTransformComp*, const SceneScaleComp*);
