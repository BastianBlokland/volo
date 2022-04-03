#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"
#include "scene_tag.h"

typedef enum {
  RendDrawFlags_None                = 0,
  RendDrawFlags_NoAutoClear         = 1 << 0,
  RendDrawFlags_NoInstanceFiltering = 1 << 1,
} RendDrawFlags;

/**
 * Low level api for submitting draws.
 * In most cases the scene apis should be preferred (SceneRenderable / SceneRenderableUnique).
 */
ecs_comp_extern(RendDrawComp);

/**
 * Add a new draw component to the given entity.
 */
RendDrawComp* rend_draw_create(EcsWorld*, EcsEntityId entity, RendDrawFlags);

/**
 * Get the amount of instances added to this draw.
 */
u32 rend_draw_instance_count(const RendDrawComp*);

/**
 * Update the graphic asset used for the draw.
 */
void rend_draw_set_graphic(RendDrawComp*, EcsEntityId graphic);

/**
 * Set a camera filter so only that specific camera will render this draw.
 */
void rend_draw_set_camera_filter(RendDrawComp*, EcsEntityId camera);

/**
 * Override the vertex count for the draw.
 * NOTE: Pass 0 to use the vertex-count as specified by the graphic.
 */
void rend_draw_set_vertex_count(RendDrawComp*, u32 vertexCount);

/**
 * Set the 'per draw' data.
 */
void rend_draw_set_data(RendDrawComp*, Mem data);

/**
 * Add a new instance to the given draw.
 * NOTE: All instances need to use the same data-size.
 * NOTE: Tags and bounds are used to filter the draws per camera.
 */
void rend_draw_add_instance(RendDrawComp*, Mem data, SceneTags, GeoBox aabb);
