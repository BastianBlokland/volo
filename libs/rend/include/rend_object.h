#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"

// Forward declare from 'scene_tag.h'.
typedef enum eSceneTags SceneTags;

typedef enum {
  RendObjectFlags_None                = 0,
  RendObjectFlags_Preload             = 1 << 0, // Load resources even if not drawn.
  RendObjectFlags_Post                = 1 << 1, // Draw in the post pass.
  RendObjectFlags_StandardGeometry    = 1 << 2, // Uses the standard instance data format.
  RendObjectFlags_Skinned             = 1 << 3,
  RendObjectFlags_Terrain             = 1 << 4,
  RendObjectFlags_VfxSprite           = 1 << 5,
  RendObjectFlags_Light               = 1 << 6,
  RendObjectFlags_FogVision           = 1 << 7,
  RendObjectFlags_Distortion          = 1 << 8,
  RendObjectFlags_Decal               = 1 << 9,
  RendObjectFlags_NoAutoClear         = 1 << 10,
  RendObjectFlags_NoInstanceFiltering = 1 << 11, // NOTE: Does not support sorting.
  RendObjectFlags_SortBackToFront     = 1 << 12,
  RendObjectFlags_SortFrontToBack     = 1 << 13,

  RendObjectFlags_Geometry = RendObjectFlags_StandardGeometry | RendObjectFlags_Terrain,
  RendObjectFlags_Sorted   = RendObjectFlags_SortBackToFront | RendObjectFlags_SortFrontToBack,
} RendObjectFlags;

typedef enum {
  RendDrawResource_Graphic,
  RendDrawResource_Texture,

  RendDrawResource_Count,
} RendDrawResource;

/**
 * Render object, low level render api.
 * In most cases the scene apis should be preferred (SceneRenderableComp).
 */
ecs_comp_extern(RendObjectComp);

/**
 * Add a new draw component to the given entity.
 */
RendObjectComp* rend_draw_create(EcsWorld*, EcsEntityId entity, RendObjectFlags);

/**
 * Query information about this draw.
 */
RendObjectFlags rend_draw_flags(const RendObjectComp*);
EcsEntityId     rend_draw_resource(const RendObjectComp*, RendDrawResource);
u32             rend_draw_instance_count(const RendObjectComp*);
u32             rend_draw_data_size(const RendObjectComp*);
u32             rend_draw_data_inst_size(const RendObjectComp*);
SceneTags       rend_draw_tag_mask(const RendObjectComp*);

/**
 * Update a draw resource
 */
void rend_draw_set_resource(RendObjectComp*, RendDrawResource, EcsEntityId asset);

/**
 * Set a camera filter so only that specific camera will render this draw.
 */
void rend_draw_set_camera_filter(RendObjectComp*, EcsEntityId camera);

/**
 * Override the vertex count for the draw.
 * NOTE: Pass 0 to use the vertex-count as specified by the graphic.
 */
void rend_draw_set_vertex_count(RendObjectComp*, u32 vertexCount);

/**
 * Clear any previously added instances.
 */
void rend_draw_clear(RendObjectComp*);

/**
 * Set the 'per draw' data.
 */
#define rend_draw_set_data_t(_DRAW_, _TYPE_)                                                       \
  ((_TYPE_*)rend_draw_set_data((_DRAW_), sizeof(_TYPE_)).ptr)

Mem rend_draw_set_data(RendObjectComp*, usize size);

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

Mem rend_draw_add_instance(RendObjectComp*, usize size, SceneTags, GeoBox aabb);
