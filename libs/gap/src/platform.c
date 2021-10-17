#include "ecs_world.h"

#include "pal_internal.h"
#include "platform_internal.h"

ecs_comp_define(GapPlatformComp) { GapPal* pal; };

static void ecs_destruct_platform_comp(void* data) {
  GapPlatformComp* comp = data;
  gap_pal_destroy(comp->pal);
}

ecs_module_init(gap_platform_module) {
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp);
}

EcsEntityId gap_platform_create(EcsWorld* world) {
  const EcsEntityId appEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, appEntity, GapPlatformComp, .pal = gap_pal_create(g_alloc_heap));
  return appEntity;
}

GapWindowId
gap_platform_window_create(GapPlatformComp* platform, const u32 width, const u32 height) {
  return gap_pal_window_create(platform->pal, width, height);
}

void gap_platform_window_destroy(GapPlatformComp* platform, const GapWindowId window) {
  gap_pal_window_destroy(platform->pal, window);
}
