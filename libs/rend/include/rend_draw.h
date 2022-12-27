#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"
#include "scene_tag.h"

typedef enum {
  RendDrawFlags_None                = 0,
  RendDrawFlags_Preload             = 1 << 0, // Load resources even if not drawn.
  RendDrawFlags_StandardGeometry    = 1 << 1, // Uses the standard instance data format.
  RendDrawFlags_Skinned             = 1 << 2,
  RendDrawFlags_Terrain             = 1 << 3,
  RendDrawFlags_Light               = 1 << 4,
  RendDrawFlags_NoAutoClear         = 1 << 5,
  RendDrawFlags_NoInstanceFiltering = 1 << 6, // NOTE: Does not support sorting.
  RendDrawFlags_SortBackToFront     = 1 << 7,
  RendDrawFlags_SortFrontToBack     = 1 << 8,

  RendDrawFlags_Geometry = RendDrawFlags_StandardGeometry | RendDrawFlags_Terrain,
  RendDrawFlags_Sorted   = RendDrawFlags_SortBackToFront | RendDrawFlags_SortFrontToBack,
} RendDrawFlags;

/**
 * Low level api for submitting draws.
 * In most cases the scene apis should be preferred (SceneRenderableComp).
 */
ecs_comp_extern(RendDrawComp);

/**
 * Add a new draw component to the given entity.
 */
RendDrawComp* rend_draw_create(EcsWorld*, EcsEntityId entity, RendDrawFlags);

/**
 * Query information about this draw.
 */
RendDrawFlags rend_draw_flags(const RendDrawComp*);
EcsEntityId   rend_draw_graphic(const RendDrawComp*);
u32           rend_draw_instance_count(const RendDrawComp*);
u32           rend_draw_data_size(const RendDrawComp*);
u32           rend_draw_data_inst_size(const RendDrawComp*);

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
 * Clear any previously added instances.
 */
void rend_draw_clear(RendDrawComp*);

/**
 * Set the 'per draw' data.
 */
#define rend_draw_set_data_t(_DRAW_, _TYPE_)                                                       \
  ((_TYPE_*)rend_draw_set_data((_DRAW_), sizeof(_TYPE_)).ptr)

Mem rend_draw_set_data(RendDrawComp*, usize size);

/**
 * Add a new instance to the given draw.
 * NOTE: Invalidates pointers from previous calls to this api.
 * NOTE: All instances need to use the same data-size.
 * NOTE: Tags and bounds are used to filter the draws per camera.
 * NOTE: Data size has to be consistent between all instances and across frames.
 * NOTE: Returned pointer is always at least 16bit aligned, stronger alignment cannot be guaranteed.
 */
#define rend_draw_add_instance_t(_DRAW_, _TYPE_, _TAGS_, _AABB_)                                   \
  ((_TYPE_*)rend_draw_add_instance((_DRAW_), sizeof(_TYPE_), (_TAGS_), (_AABB_)).ptr)

Mem rend_draw_add_instance(RendDrawComp*, usize size, SceneTags, GeoBox aabb);
