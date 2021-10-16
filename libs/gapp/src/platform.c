#include "ecs_world.h"

#include "pal_internal.h"
#include "platform_internal.h"

ecs_comp_define(GAppPlatformComp) { GAppPal* pal; };

static void ecs_destruct_platform_comp(void* data) {
  GAppPlatformComp* comp = data;
  gapp_pal_destroy(comp->pal);
}

ecs_module_init(gapp_platform_module) {
  ecs_register_comp(GAppPlatformComp, .destructor = ecs_destruct_platform_comp);
}

EcsEntityId gapp_platform_create(EcsWorld* world) {
  const EcsEntityId appEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, appEntity, GAppPlatformComp, .pal = gapp_pal_create(g_alloc_heap));
  return appEntity;
}
