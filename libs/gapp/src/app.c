#include "ecs_world.h"

#include "app_internal.h"
#include "pal_internal.h"

ecs_comp_define(GAppAppComp) { GAppPal* pal; };

static void ecs_destruct_app_comp(void* data) {
  GAppAppComp* comp = data;
  gapp_pal_destroy(comp->pal);
}

ecs_module_init(gapp_app_module) {
  ecs_register_comp(GAppAppComp, .destructor = ecs_destruct_app_comp);
}

EcsEntityId gapp_app_create(EcsWorld* world) {
  const EcsEntityId appEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, appEntity, GAppAppComp, .pal = gapp_pal_create(g_alloc_heap));
  return appEntity;
}
