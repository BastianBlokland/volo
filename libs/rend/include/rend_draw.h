#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"
#include "scene_tag.h"

/**
 * Low level api for submitting draws.
 * In most cases the scene apis should be preferred (SceneRenderable / SceneRenderableUnique).
 */
ecs_comp_extern(RendDrawComp);

/**
 * Add a new draw component to the given entity.
 */
RendDrawComp* rend_draw_create(EcsWorld*, EcsEntityId entity);

/**
 * Update the graphic asset used for the draw.
 */
void rend_draw_set_graphic(RendDrawComp*, EcsEntityId graphic);

/**
 * Override the vertex count for the draw.
 * NOTE: Pass 0 to use the vertex-count as specified by the graphic.
 */
void rend_draw_set_vertex_count(RendDrawComp*, u32 vertexCount);

/**
 * Update the data size per instance for the given draw.
 * NOTE: Clears all added instances.
 */
void rend_draw_set_data_size(RendDrawComp*, u32 size);

/**
 * Add a new instance to the given draw.
 * NOTE: Tags and bounds are used to filter the draws per camera.
 */
Mem rend_draw_add_instance(RendDrawComp*, SceneTags, GeoBox aabb);
