#include "core_sentinel.h"
#include "core_signal.h"
#include "ecs_world.h"
#include "gap_window.h"

#include "pal_internal.h"

typedef enum {
  GapWindowRequests_None   = 0,
  GapWindowRequests_Create = 1 << 0,
  GapWindowRequests_Close  = 1 << 1,
} GapWindowRequests;

ecs_comp_define(GapPlatformComp) { GapPal* pal; };

ecs_comp_define(GapWindowComp) {
  GapWindowId       id;
  GapWindowFlags    flags;
  GapWindowEvents   events;
  GapWindowRequests requests;
};

static void ecs_destruct_platform_comp(void* data) {
  GapPlatformComp* comp = data;
  gap_pal_destroy(comp->pal);
}

static bool window_should_close(GapWindowComp* window) {
  if (window->requests & GapWindowRequests_Close) {
    return true;
  }
  if (window->flags & GapWindowFlags_CloseOnInterupt && signal_is_received(Signal_Interupt)) {
    return true;
  }
  return false;
}

static void window_update(
    EcsWorld*         world,
    GapPlatformComp*  platform,
    GapWindowComp*    window,
    const EcsEntityId windowEntity) {

  // Clear the events of the previous tick.
  window->events = GapWindowEvents_None;

  if (window->requests & GapWindowRequests_Create) {
    window->id = gap_pal_window_create(platform->pal, 640, 480);
  }

  if (window_should_close(window)) {
    gap_pal_window_destroy(platform->pal, window->id);
    window->events |= GapWindowEvents_Closed;
    ecs_world_entity_destroy(world, windowEntity);
  }

  // All requests have been handled.
  window->requests = GapWindowRequests_None;
}

ecs_view_define(GapPlatformView) { ecs_access_write(GapPlatformComp); };
ecs_view_define(GapWindowView) { ecs_access_write(GapWindowComp); };

ecs_system_define(GapUpdateSys) {
  EcsIterator*     platformItr = ecs_view_itr_first(ecs_world_view_t(world, GapPlatformView));
  GapPlatformComp* platform;

  if (platformItr) {
    platform = ecs_view_write_t(platformItr, GapPlatformComp);
  } else {
    const EcsEntityId platformEntity = ecs_world_entity_create(world);
    platform                         = ecs_world_add_t(
        world, platformEntity, GapPlatformComp, .pal = gap_pal_create(g_alloc_heap));
  }

  EcsView* windowView = ecs_world_view_t(world, GapWindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {

    const EcsEntityId windowEntity = ecs_view_entity(itr);
    GapWindowComp*    window       = ecs_view_write_t(itr, GapWindowComp);
    window_update(world, platform, window, windowEntity);
  }
}

ecs_module_init(gap_window_module) {
  ecs_register_comp(GapWindowComp);
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(GapPlatformView);
  ecs_register_view(GapWindowView);

  ecs_register_system(GapUpdateSys, ecs_view_id(GapPlatformView), ecs_view_id(GapWindowView));
}

EcsEntityId gap_window_open(EcsWorld* world, const GapWindowFlags flags) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      windowEntity,
      GapWindowComp,
      .id       = sentinel_u32,
      .flags    = flags,
      .requests = GapWindowRequests_Create);
  return windowEntity;
}

void gap_window_close(GapWindowComp* window) { window->requests |= GapWindowRequests_Close; }

GapWindowEvents gap_window_events(const GapWindowComp* window) { return window->events; }
