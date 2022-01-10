#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Indicates that the entity should be rendered with the given graphic.
 *
 * The following instance data will automatically be provided to the graphic:
 * - Position (if the entity has a SceneTransformComp)
 * - Rotation (if the entity has a SceneTransformComp)
 *
 * Draws will automatically be batched with other renderables using the same graphic.
 */
ecs_comp_extern_public(SceneRenderableComp) { EcsEntityId graphic; };

/**
 * Indicates that the entity should be rendered with the given graphic.
 *
 * Draws will not be batched and no instance data will be automatically provided.
 */
ecs_comp_extern_public(SceneRenderableUniqueComp) { EcsEntityId graphic; };
