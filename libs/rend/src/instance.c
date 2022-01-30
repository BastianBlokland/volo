#include "ecs_world.h"
#include "rend_register.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_tag.h"
#include "scene_transform.h"

#include "draw_internal.h"
#include "resource_internal.h"

#define rend_instance_max_res_requests 16

typedef struct {
  ALIGNAS(16)
  GeoVector posAndScale; // xyz: position, w: scale.
  GeoQuat   rot;
} RendInstanceData;

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneBoundsComp);
}

ecs_view_define(RenderableUniqueView) {
  ecs_access_read(SceneRenderableUniqueComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_write(RendDrawComp);
}

ecs_view_define(ResourceView) { ecs_access_write(RendResComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

/**
 * Request the given graphic entity to be loaded.
 */
static void rend_instance_request_graphic(
    EcsWorld* world, const EcsEntityId entity, EcsIterator* graphicItr, u32* numRequests) {
  /**
   * If the graphic resource is already loaded then tell the resource system we're still using it
   * (so it won't be unloaded). If its not loaded then start loading it.
   */
  if (LIKELY(ecs_view_maybe_jump(graphicItr, entity))) {
    rend_resource_mark_used(ecs_view_write_t(graphicItr, RendResComp));
    return;
  }
  if (++*numRequests < rend_instance_max_res_requests) {
    rend_resource_request(world, entity);
  }
}

ecs_system_define(RendInstanceRequestGraphicSys) {
  u32 numRequests = 0;

  EcsIterator* graphicResItr = ecs_view_itr(ecs_world_view_t(world, ResourceView));

  // Request the graphic resource for SceneRenderableComp's to be loaded.
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  for (EcsIterator* itr = ecs_view_itr(renderableView); ecs_view_walk(itr);) {
    const SceneRenderableComp* comp = ecs_view_read_t(itr, SceneRenderableComp);
    rend_instance_request_graphic(world, comp->graphic, graphicResItr, &numRequests);
  }

  // Request the graphic resource for SceneRenderableUniqueComp's to be loaded.
  EcsView* renderableUniqueView = ecs_world_view_t(world, RenderableUniqueView);
  for (EcsIterator* itr = ecs_view_itr(renderableUniqueView); ecs_view_walk(itr);) {
    const SceneRenderableUniqueComp* comp = ecs_view_read_t(itr, SceneRenderableUniqueComp);
    rend_instance_request_graphic(world, comp->graphic, graphicResItr, &numRequests);
  }
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  EcsView* drawView       = ecs_world_view_t(world, DrawView);

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
    const SceneTags           tags          = tagComp ? tagComp->tags : SceneTags_Default;

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendDrawComp))) {
      RendDrawComp* draw = rend_draw_create(world, renderable->graphic);
      rend_draw_set_graphic(draw, renderable->graphic);
      rend_draw_set_data_size(draw, sizeof(RendInstanceData));
      continue;
    }

    ecs_view_jump(drawItr, renderable->graphic);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    const GeoVector position = transformComp ? transformComp->position : geo_vector(0);
    const GeoQuat   rotation = transformComp ? transformComp->rotation : geo_quat_ident;
    const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;
    const GeoBox    aabb     = boundsComp
                            ? geo_box_transform3(&boundsComp->local, position, rotation, scale)
                            : geo_box_inverted3();

    *mem_as_t(rend_draw_add_instance(draw, tags, aabb), RendInstanceData) = (RendInstanceData){
        .posAndScale = geo_vector(position.x, position.y, position.z, scale),
        .rot         = rotation,
    };
  }
}

ecs_system_define(RendInstanceFillUniqueDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableUniqueView);
  for (EcsIterator* itr = ecs_view_itr(renderableView); ecs_view_walk(itr);) {
    const SceneRenderableUniqueComp* renderable = ecs_view_read_t(itr, SceneRenderableUniqueComp);
    if (renderable->flags & SceneRenderable_Hide) {
      continue;
    }

    const SceneTagComp* tagComp = ecs_view_read_t(itr, SceneTagComp);
    const Mem           data    = scene_renderable_unique_data_get(renderable);
    const SceneTags     tags    = tagComp ? tagComp->tags : SceneTags_Default;

    RendDrawComp* draw = ecs_view_write_t(itr, RendDrawComp);
    if (UNLIKELY(!draw)) {
      rend_draw_create(world, ecs_view_entity(itr));
      continue;
    }

    rend_draw_set_graphic(draw, renderable->graphic);
    rend_draw_set_vertex_count(draw, renderable->vertexCountOverride);
    rend_draw_set_data_size(draw, (u32)data.size);
    const GeoBox aabb = geo_box_inverted3(); // No bounds known for unique draws.
    mem_cpy(rend_draw_add_instance(draw, tags, aabb), data);
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_view(RenderableView);
  ecs_register_view(RenderableUniqueView);
  ecs_register_view(ResourceView);
  ecs_register_view(DrawView);

  ecs_register_system(
      RendInstanceRequestGraphicSys,
      ecs_view_id(RenderableView),
      ecs_view_id(RenderableUniqueView),
      ecs_view_id(ResourceView));

  ecs_register_system(RendInstanceFillDrawsSys, ecs_view_id(RenderableView), ecs_view_id(DrawView));
  ecs_register_system(RendInstanceFillUniqueDrawsSys, ecs_view_id(RenderableUniqueView));

  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
  ecs_order(RendInstanceFillUniqueDrawsSys, RendOrder_DrawCollect);
}
