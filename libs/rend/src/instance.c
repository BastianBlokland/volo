#include "core_alloc.h"
#include "ecs_world.h"
#include "rend_instance.h"
#include "rend_register.h"
#include "scene_transform.h"

#include "painter_internal.h"
#include "resource_internal.h"

typedef struct {
  ALIGNAS(16)
  GeoVector position;
  GeoQuat   rotation;
} RendInstanceData;

ecs_comp_define_public(RendInstanceComp);
ecs_comp_define_public(RendInstanceCustomComp);

ecs_view_define(InstanceView) {
  ecs_access_read(RendInstanceComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(InstanceCustomView) { ecs_access_read(RendInstanceCustomComp); }

ecs_view_define(PainterDrawView) { ecs_access_write(RendPainterDrawComp); }

ecs_system_define(RendInstanceRequestResourcesSys) {
  // Request the graphic resource for RendInstanceComp's to be loaded.
  EcsView* instView = ecs_world_view_t(world, InstanceView);
  for (EcsIterator* itr = ecs_view_itr(instView); ecs_view_walk(itr);) {
    const RendInstanceComp* instComp = ecs_view_read_t(itr, RendInstanceComp);
    rend_resource_request(world, instComp->graphic);
  }

  // Request the graphic resource for RendInstanceCustomComp's to be loaded.
  EcsView* instCustomView = ecs_world_view_t(world, InstanceCustomView);
  for (EcsIterator* itr = ecs_view_itr(instCustomView); ecs_view_walk(itr);) {
    const RendInstanceCustomComp* instCustomComp = ecs_view_read_t(itr, RendInstanceCustomComp);
    rend_resource_request(world, instCustomComp->graphic);
  }
}

ecs_system_define(RendInstanceClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, PainterDrawView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    dynarray_clear(&ecs_view_write_t(itr, RendPainterDrawComp)->instances);
  }
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView* instView = ecs_world_view_t(world, InstanceView);
  EcsView* drawView = ecs_world_view_t(world, PainterDrawView);

  EcsIterator* drawItr = ecs_view_itr(drawView);
  for (EcsIterator* instItr = ecs_view_itr(instView); ecs_view_walk(instItr);) {
    const RendInstanceComp*   instComp      = ecs_view_read_t(instItr, RendInstanceComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(instItr, SceneTransformComp);

    if (UNLIKELY(!ecs_world_has_t(world, instComp->graphic, RendPainterDrawComp))) {
      ecs_world_add_t(
          world,
          instComp->graphic,
          RendPainterDrawComp,
          .graphic   = instComp->graphic,
          .instances = dynarray_create_t(g_alloc_heap, RendInstanceData, 128));
      continue;
    }
    ecs_view_jump(drawItr, instComp->graphic);
    RendPainterDrawComp* drawComp = ecs_view_write_t(drawItr, RendPainterDrawComp);

    *dynarray_push_t(&drawComp->instances, RendInstanceData) = (RendInstanceData){
        .position = transformComp ? transformComp->position : geo_vector(0),
        .rotation = transformComp ? transformComp->rotation : geo_quat_ident,
    };
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_comp(RendInstanceComp);
  ecs_register_comp(RendInstanceCustomComp);

  ecs_register_view(InstanceView);
  ecs_register_view(InstanceCustomView);
  ecs_register_view(PainterDrawView);

  ecs_register_system(
      RendInstanceRequestResourcesSys, ecs_view_id(InstanceView), ecs_view_id(InstanceCustomView));

  ecs_register_system(RendInstanceClearDrawsSys, ecs_view_id(PainterDrawView));

  ecs_register_system(
      RendInstanceFillDrawsSys, ecs_view_id(InstanceView), ecs_view_id(PainterDrawView));

  ecs_order(RendInstanceClearDrawsSys, RendOrder_DrawCollect - 1);
  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
}
