#pragma once
#include "core/time.h"
#include "ecs/module.h"
#include "geo/color.h"

/**
 * Indicates that the entity should be rendered with the given graphic.
 *
 * The following instance data will automatically be provided to the graphic:
 * - Position (if the entity has a SceneTransformComp)
 * - Rotation (if the entity has a SceneTransformComp)
 * - Scale (if the entity has a SceneTransformComp)
 *
 * Draws will automatically be batched with other renderables using the same graphic.
 */
ecs_comp_extern_public(SceneRenderableComp) {
  EcsEntityId graphic;
  GeoColor    emissive; // Normalized.
  GeoColor    color;    // Normalized.
};

ecs_comp_extern_public(SceneRenderableFadeinComp) { TimeDuration duration, elapsed; };
ecs_comp_extern_public(SceneRenderableFadeoutComp) { TimeDuration duration; };
