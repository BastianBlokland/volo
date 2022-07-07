#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"

#include "painter_internal.h"
#include "platform_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"

ecs_comp_define(RendResetComp);

ecs_view_define(PainterView) { ecs_access_with(RendPainterComp); }
ecs_view_define(ResourceView) { ecs_access_read(RendResComp); }

ecs_system_define(RendResetSys) {
  if (!rend_will_reset(world)) {
    return;
  }

  log_i("Resetting renderer");

  rend_platform_teardown(world);

  EcsView* painterView = ecs_world_view_t(world, PainterView);
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    rend_painter_teardown(world, ecs_view_entity(itr));
  }

  EcsView* resourceView = ecs_world_view_t(world, ResourceView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    const RendResComp* rendRes = ecs_view_read_t(itr, RendResComp);
    rend_res_teardown(world, rendRes, ecs_view_entity(itr));
  }

  ecs_world_remove_t(world, ecs_world_global(world), RendResetComp);
}

ecs_module_init(rend_reset_module) {
  ecs_register_comp_empty(RendResetComp);

  ecs_register_view(PainterView);
  ecs_register_view(ResourceView);

  ecs_register_system(RendResetSys, ecs_view_id(PainterView), ecs_view_id(ResourceView));

  ecs_order(RendResetSys, RendOrder_Reset);
}

bool rend_will_reset(EcsWorld* world) {
  return ecs_world_has_t(world, ecs_world_global(world), RendResetComp);
}

void rend_reset(EcsWorld* world) {
  ecs_utils_maybe_add_t(world, ecs_world_global(world), RendResetComp);
}
