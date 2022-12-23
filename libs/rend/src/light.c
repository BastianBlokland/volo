#include "ecs_world.h"
#include "rend_light.h"

ecs_comp_define_public(RendLightGlobalComp);

ecs_view_define(LightGlobalView) { ecs_access_maybe_write(RendLightGlobalComp); }

static RendLightGlobalComp* rend_light_global_create(EcsWorld* world) {
  const EcsEntityId    global      = ecs_world_global(world);
  RendLightGlobalComp* lightGlobal = ecs_world_add_t(world, global, RendLightGlobalComp);
  rend_light_global_to_default(lightGlobal);
  return lightGlobal;
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

void rend_light_global_to_default(RendLightGlobalComp* lightGlobal) {
  lightGlobal->sunLight         = geo_color(1.0f, 0.9f, 0.7f, 1.0f);
  lightGlobal->sunShininess     = 16;
  lightGlobal->sunRotation      = geo_quat_from_euler(geo_vector(65, 15, 0));
  lightGlobal->ambientIntensity = 0.1f;
  lightGlobal->reflectFrac      = 0.25f;
}
