#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "scene.h"

typedef enum eRendObjectFlags {
  RendObjectFlags_None                = 0,
  RendObjectFlags_Preload             = 1 << 0, // Load resources even if not drawn.
  RendObjectFlags_NoAutoClear         = 1 << 1,
  RendObjectFlags_NoInstanceFiltering = 1 << 2, // NOTE: Does not support sorting.
  RendObjectFlags_SortBackToFront     = 1 << 3,
  RendObjectFlags_SortFrontToBack     = 1 << 4,

  RendObjectFlags_Sorted = RendObjectFlags_SortBackToFront | RendObjectFlags_SortFrontToBack,
} RendObjectFlags;

typedef enum eRendObjectRes {
  RendObjectRes_Graphic,
  RendObjectRes_GraphicShadow,
  RendObjectRes_GraphicDebugSkinning,
  RendObjectRes_GraphicDebugWireframe,
  RendObjectRes_Texture,

  RendObjectRes_Count,
} RendObjectRes;

/**
 * Render object, low level render api.
 * In most cases the scene apis should be preferred (SceneRenderableComp).
 */
ecs_comp_extern(RendObjectComp);

/**
 * Add a render-object to the given entity.
 */
RendObjectComp* rend_object_create(EcsWorld*, EcsEntityId entity, RendObjectFlags);

/**
 * Query information about this object.
 */
RendObjectFlags rend_object_flags(const RendObjectComp*);
EcsEntityId     rend_object_resource(const RendObjectComp*, RendObjectRes);
u32             rend_object_instance_count(const RendObjectComp*);
u32             rend_object_data_size(const RendObjectComp*);
u32             rend_object_data_inst_size(const RendObjectComp*);
SceneTags       rend_object_tag_mask(const RendObjectComp*);
u8              rend_object_alpha_tex_index(const RendObjectComp*); // sentinel_u8 if unused.

/**
 * Update a object resource
 */
void rend_object_set_resource(RendObjectComp*, RendObjectRes, EcsEntityId asset);

/**
 * Set a camera filter so only that specific camera will render this object.
 */
void rend_object_set_camera_filter(RendObjectComp*, EcsEntityId camera);

/**
 * Override the vertex count for the object.
 * NOTE: Pass 0 to use the vertex-count as specified by the graphic.
 */
void rend_object_set_vertex_count(RendObjectComp*, u32 vertexCount);

/**
 * Update the alpha texture index from the main graphic.
 * The alpha texture is passed as a draw-image to the shadow graphic draw.
 */
void rend_object_set_alpha_tex_index(RendObjectComp*, u8 alphaTexIndex);

/**
 * Clear any previously added instances.
 */
void rend_object_clear(RendObjectComp*);

/**
 * Set the 'per draw' data.
 */
#define rend_object_set_data_t(_OBJ_, _TYPE_)                                                      \
  ((_TYPE_*)rend_object_set_data((_OBJ_), sizeof(_TYPE_)).ptr)

Mem rend_object_set_data(RendObjectComp*, usize size);

/**
 * Add a new instance to the given object.
 * NOTE: Invalidates pointers from previous calls to this api.
 * NOTE: All instances need to use the same data-size.
 * NOTE: Tags and bounds are used to filter the object per camera.
 * NOTE: Data size has to be consistent between all instances and across frames.
 * NOTE: Returned pointer is always at least 16bit aligned, stronger alignment cannot be guaranteed.
 */
#define rend_object_add_instance_t(_OBJ_, _TYPE_, _TAGS_, _AABB_)                                  \
  ((_TYPE_*)rend_object_add_instance((_OBJ_), sizeof(_TYPE_), (_TAGS_), (_AABB_)).ptr)

Mem rend_object_add_instance(RendObjectComp*, usize size, SceneTags, GeoBox aabb);
