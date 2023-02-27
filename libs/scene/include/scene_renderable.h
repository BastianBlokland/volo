#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

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
  f32         alpha;
};

ecs_comp_extern_public(SceneRenderableFadeoutComp) { TimeDuration duration; };
