#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "scene_renderable.h"
#include "scene_transform.h"

#include "painter_internal.h"
#include "resource_internal.h"

typedef struct {
  ALIGNAS(16)
  GeoVector position;
  GeoQuat   rotation;
} RendInstanceData;

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(RenderableUniqueView) {
  ecs_access_read(SceneRenderableUniqueComp);
  ecs_access_maybe_write(RendPainterDrawComp);
}

ecs_view_define(PainterDrawView) { ecs_access_write(RendPainterDrawComp); }

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

ecs_system_define(RendInstanceClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, PainterDrawView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    dynarray_clear(&ecs_view_write_t(itr, RendPainterDrawComp)->instances);
  }
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  EcsView* drawView       = ecs_world_view_t(world, PainterDrawView);

  EcsIterator* drawItr = ecs_view_itr(drawView);
  for (EcsIterator* renderableItr = ecs_view_itr(renderableView); ecs_view_walk(renderableItr);) {
    const SceneRenderableComp* renderableComp = ecs_view_read_t(renderableItr, SceneRenderableComp);
    const SceneTransformComp*  transformComp  = ecs_view_read_t(renderableItr, SceneTransformComp);

    if (UNLIKELY(!ecs_world_has_t(world, renderableComp->graphic, RendPainterDrawComp))) {
      ecs_world_add_t(
          world,
          renderableComp->graphic,
          RendPainterDrawComp,
          .graphic   = renderableComp->graphic,
          .instances = dynarray_create_t(g_alloc_heap, RendInstanceData, 128));
      continue;
    }

    ecs_view_jump(drawItr, renderableComp->graphic);
    RendPainterDrawComp* drawComp = ecs_view_write_t(drawItr, RendPainterDrawComp);

    *dynarray_push_t(&drawComp->instances, RendInstanceData) = (RendInstanceData){
        .position = transformComp ? transformComp->position : geo_vector(0),
        .rotation = transformComp ? transformComp->rotation : geo_quat_ident,
    };
  }
}

ecs_system_define(RendInstanceFillUniqueDrawsSys) {
  EcsView* renderableView = ecs_world_view_t(world, RenderableUniqueView);
  for (EcsIterator* renderableItr = ecs_view_itr(renderableView); ecs_view_walk(renderableItr);) {

    const SceneRenderableUniqueComp* renderableComp =
        ecs_view_read_t(renderableItr, SceneRenderableUniqueComp);
    RendPainterDrawComp* drawComp = ecs_view_write_t(renderableItr, RendPainterDrawComp);

    if (UNLIKELY(!drawComp)) {
      ecs_world_add_t(
          world,
          ecs_view_entity(renderableItr),
          RendPainterDrawComp,
          .graphic   = renderableComp->graphic,
          .instances = dynarray_create(g_alloc_heap, 1, 16, 0));
      continue;
    }

    diag_assert(!drawComp->instances.size); // Every RenderableUnique should have its own draw.
    diag_assert(drawComp->graphic == renderableComp->graphic);

    // Set overrides.
    drawComp->vertexCountOverride = renderableComp->vertexCountOverride;

    // Set instance data.
    const Mem data = mem_slice(renderableComp->instDataMem, 0, renderableComp->instDataSize);
    drawComp->instances.stride = (u32)math_max(data.size, 16);
    mem_cpy(dynarray_push(&drawComp->instances, 1), data);
  }
}

ecs_module_init(rend_instance_module) {

  ecs_register_view(RenderableView);
  ecs_register_view(RenderableUniqueView);
  ecs_register_view(PainterDrawView);

  ecs_register_system(
      RendInstanceRequestResourcesSys,
      ecs_view_id(RenderableView),
      ecs_view_id(RenderableUniqueView));

  ecs_register_system(RendInstanceClearDrawsSys, ecs_view_id(PainterDrawView));
  ecs_register_system(
      RendInstanceFillDrawsSys, ecs_view_id(RenderableView), ecs_view_id(PainterDrawView));
  ecs_register_system(RendInstanceFillUniqueDrawsSys, ecs_view_id(RenderableUniqueView));

  ecs_order(RendInstanceClearDrawsSys, RendOrder_DrawCollect - 1);
  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
  ecs_order(RendInstanceFillUniqueDrawsSys, RendOrder_DrawCollect);
}
