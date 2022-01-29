#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneRenderable_Hide = 1 << 0,
} SceneRenderableFlags;

/**
 * Indicates that the entity should be rendered with the given graphic.
 *
 * The following instance data will automatically be provided to the graphic:
 * - Position (if the entity has a SceneTransformComp)
 * - Rotation (if the entity has a SceneTransformComp)
 *
 * Draws will automatically be batched with other renderables using the same graphic.
 */
ecs_comp_extern_public(SceneRenderableComp) {
  SceneRenderableFlags flags;
  EcsEntityId          graphic;
};

/**
 * Indicates that the entity should be rendered with the given graphic.
 *
 * Draws will not be batched and no instance data will be automatically provided.
 */
ecs_comp_extern_public(SceneRenderableUniqueComp) {
  SceneRenderableFlags flags;
  EcsEntityId          graphic;
  u32                  vertexCountOverride;
  Mem                  instDataMem;
  usize                instDataSize;
};

Mem scene_renderable_unique_data_get(const SceneRenderableUniqueComp*);
Mem scene_renderable_unique_data_set(SceneRenderableUniqueComp*, usize size);
