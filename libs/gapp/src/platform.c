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

GAppWindowId
gapp_platform_window_create(GAppPlatformComp* platform, const u32 width, const u32 height) {
  return gapp_pal_window_create(platform->pal, width, height);
}

void gapp_platform_window_destroy(GAppPlatformComp* platform, const GAppWindowId window) {
  gapp_pal_window_destroy(platform->pal, window);
}
