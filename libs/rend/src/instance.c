#include "ecs_world.h"
#include "rend_register.h"
#include "scene_renderable.h"
#include "scene_tag.h"
#include "scene_transform.h"

#include "draw_internal.h"
#include "resource_internal.h"

typedef struct {
  ALIGNAS(16)
  GeoVector position;
  GeoQuat   rotation;
} RendInstanceData;

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(RenderableUniqueView) {
  ecs_access_read(SceneRenderableUniqueComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_write(RendDrawComp);
}

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendInstanceRequestResourcesSys) {
  // Request the graphic resource for SceneRenderableComp's to be loaded.
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  for (EcsIterator* itr = ecs_view_itr(renderableView); ecs_view_walk(itr);) {
    const SceneRenderableComp* comp = ecs_view_read_t(itr, SceneRenderableComp);
    rend_resource_request(world, comp->graphic);
  }

  // Request the graphic resource for SceneRenderableUniqueComp's to be loaded.
  EcsView* renderableUniqueView = ecs_world_view_t(world, RenderableUniqueView);
  for (EcsIterator* itr = ecs_view_itr(renderableUniqueView); ecs_view_walk(itr);) {
    const SceneRenderableUniqueComp* comp = ecs_view_read_t(itr, SceneRenderableUniqueComp);
    rend_resource_request(world, comp->graphic);
  }
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  EcsView* drawView       = ecs_world_view_t(world, DrawView);

  EcsIterator* drawItr = ecs_view_itr(drawView);
  for (EcsIterator* renderableItr = ecs_view_itr(renderableView); ecs_view_walk(renderableItr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(renderableItr, SceneRenderableComp);
    const SceneTagComp*        tagComp    = ecs_view_read_t(renderableItr, SceneTagComp);
    const SceneTransformComp*  transform  = ecs_view_read_t(renderableItr, SceneTransformComp);

    const SceneTags tags = tagComp ? tagComp->tags : SceneTag_None;

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendDrawComp))) {
      RendDrawComp* draw = rend_draw_create(world, renderable->graphic);
      rend_draw_set_graphic(draw, renderable->graphic);
      rend_draw_set_data_size(draw, sizeof(RendInstanceData));
      continue;
    }

    ecs_view_jump(drawItr, renderable->graphic);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    *mem_as_t(rend_draw_add_instance(draw, tags), RendInstanceData) = (RendInstanceData){
        .position = transform ? transform->position : geo_vector(0),
        .rotation = transform ? transform->rotation : geo_quat_ident,
    };
  }
}

ecs_system_define(RendInstanceFillUniqueDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableUniqueView);
  for (EcsIterator* itr = ecs_view_itr(renderableView); ecs_view_walk(itr);) {

    const SceneRenderableUniqueComp* renderable = ecs_view_read_t(itr, SceneRenderableUniqueComp);
    const SceneTagComp*              tagComp    = ecs_view_read_t(itr, SceneTagComp);

    const Mem       data = scene_renderable_unique_data_get(renderable);
    const SceneTags tags = tagComp ? tagComp->tags : SceneTag_None;

    RendDrawComp* draw = ecs_view_write_t(itr, RendDrawComp);
    if (UNLIKELY(!draw)) {
      rend_draw_create(world, ecs_view_entity(itr));
      continue;
    }

    rend_draw_set_graphic(draw, renderable->graphic);
    rend_draw_set_vertex_count(draw, renderable->vertexCountOverride);
    rend_draw_set_data_size(draw, (u32)data.size);
    mem_cpy(rend_draw_add_instance(draw, tags), data);
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_view(RenderableView);
  ecs_register_view(RenderableUniqueView);
  ecs_register_view(DrawView);

  ecs_register_system(
      RendInstanceRequestResourcesSys,
      ecs_view_id(RenderableView),
      ecs_view_id(RenderableUniqueView));

  ecs_register_system(RendInstanceFillDrawsSys, ecs_view_id(RenderableView), ecs_view_id(DrawView));
  ecs_register_system(RendInstanceFillUniqueDrawsSys, ecs_view_id(RenderableUniqueView));

  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
  ecs_order(RendInstanceFillUniqueDrawsSys, RendOrder_DrawCollect);
}
