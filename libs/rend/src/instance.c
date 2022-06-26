#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_transform.h"

#include "draw_internal.h"

#define rend_instance_max_draw_create 16
#define rend_instance_max_joints 64

typedef struct {
  ALIGNAS(16)
  GeoVector posAndScale; // xyz: position, w: scale.
  GeoQuat   rot;
} RendInstanceData;

ASSERT(sizeof(RendInstanceData) == 32, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  GeoVector posAndScale; // xyz: position, w: scale.
  GeoQuat   rot;
  GeoMatrix jointDelta[rend_instance_max_joints];
} RendInstanceSkinnedData;

ASSERT(sizeof(RendInstanceSkinnedData) == 4128, "Size needs to match the size defined in glsl");

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneBoundsComp);
  ecs_access_read(SceneSkeletonComp);

  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

ecs_view_define(DrawView) {
  ecs_access_write(RendDrawComp);
  ecs_access_maybe_read(SceneSkeletonTemplComp);
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  EcsView* drawView       = ecs_world_view_t(world, DrawView);

  u32 createdDraws = 0;

  EcsIterator* drawItr = ecs_view_itr(drawView);
  for (EcsIterator* renderableItr = ecs_view_itr(renderableView); ecs_view_walk(renderableItr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(renderableItr, SceneRenderableComp);
    if (renderable->flags & SceneRenderable_Hide) {
      continue;
    }

    const SceneTagComp*       tagComp       = ecs_view_read_t(renderableItr, SceneTagComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(renderableItr, SceneTransformComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(renderableItr, SceneScaleComp);
    const SceneBoundsComp*    boundsComp    = ecs_view_read_t(renderableItr, SceneBoundsComp);
    const SceneSkeletonComp*  skeletonComp  = ecs_view_read_t(renderableItr, SceneSkeletonComp);
    const SceneTags           tags          = tagComp ? tagComp->tags : SceneTags_Default;

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendDrawComp))) {
      if (++createdDraws > rend_instance_max_draw_create) {
        continue; // Limit the amount of new draws to create per frame.
      }
      const RendDrawFlags flags = RendDrawFlags_StandardGeometry;
      RendDrawComp*       draw  = rend_draw_create(world, renderable->graphic, flags);
      rend_draw_set_graphic(draw, renderable->graphic);
      continue;
    }

    ecs_view_jump(drawItr, renderable->graphic);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    const GeoVector position = transformComp ? transformComp->position : geo_vector(0);
    const GeoQuat   rotation = transformComp ? transformComp->rotation : geo_quat_ident;
    const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;
    const GeoBox    aabb     = geo_box_is_inverted3(&boundsComp->local)
                                   ? geo_box_inverted3()
                                   : geo_box_transform3(&boundsComp->local, position, rotation, scale);

    const bool isSkinned = skeletonComp->jointCount != 0;
    if (isSkinned) {
      diag_assert(skeletonComp->jointCount <= rend_instance_max_joints);
      const SceneSkeletonTemplComp* templ = ecs_view_read_t(drawItr, SceneSkeletonTemplComp);
      RendInstanceSkinnedData       data  = {
          .posAndScale = geo_vector(position.x, position.y, position.z, scale),
          .rot         = rotation,
      };
      scene_skeleton_delta(skeletonComp, templ, data.jointDelta);
      rend_draw_add_instance(draw, mem_var(data), tags, aabb);
    } else /* !isSkinned */ {
      const RendInstanceData data = {
          .posAndScale = geo_vector(position.x, position.y, position.z, scale),
          .rot         = rotation,
      };
      rend_draw_add_instance(draw, mem_var(data), tags, aabb);
    }
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_view(RenderableView);
  ecs_register_view(DrawView);

  ecs_register_system(RendInstanceFillDrawsSys, ecs_view_id(RenderableView), ecs_view_id(DrawView));

  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
}
