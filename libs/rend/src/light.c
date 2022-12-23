#include "ecs_world.h"
#include "rend_light.h"

ecs_comp_define_public(RendLightGlobalComp);

ecs_view_define(LightGlobalView) { ecs_access_maybe_write(RendLightGlobalComp); }

static RendLightGlobalComp* rend_light_global_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      RendLightGlobalComp,
      .sunLight = geo_color(1.0f, 0.9f, 0.7f, 0));
}

ecs_system_define(RendLightInitSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), RendLightGlobalComp)) {
    rend_light_global_create(world);
  }
}

ecs_module_init(rend_light_module) {
  ecs_register_comp(RendLightGlobalComp);

  ecs_register_view(LightGlobalView);

  ecs_register_system(RendLightInitSys, ecs_view_id(LightGlobalView));
}
