#include "core_signal.h"
#include "ecs_world.h"
#include "gapp_window.h"

#include "platform_internal.h"

typedef enum {
  GAppWindowRequests_None   = 0,
  GAppWindowRequests_Create = 1 << 0,
  GAppWindowRequests_Close  = 1 << 1,
} GAppWindowRequests;

ecs_comp_define(GAppWindowComp) {
  GAppWindowFlags    flags;
  GAppWindowEvents   events;
  GAppWindowRequests requests;
};

static bool window_should_close(GAppWindowComp* window) {
  if (window->requests & GAppWindowRequests_Close) {
    return true;
  }
  if (window->flags & GAppWindowFlags_CloseOnInterupt && signal_is_received(Signal_Interupt)) {
    return true;
  }
  return false;
}

static void window_update(EcsWorld* world, GAppWindowComp* window, const EcsEntityId windowEntity) {
  // Clear the events of the previous tick.
  window->events = GAppWindowEvents_None;

  if (window_should_close(window)) {
    window->events |= GAppWindowEvents_Closed;
    ecs_world_entity_destroy(world, windowEntity);
  }

  // All requests have been handled.
  window->requests = GAppWindowRequests_None;
}

ecs_view_define(GAppPlatformView) { ecs_access_write(GAppPlatformComp); };
ecs_view_define(GAppWindowView) { ecs_access_write(GAppWindowComp); };

ecs_system_define(GAppUpdateSys) {
  EcsIterator* platformItr = ecs_view_itr_first(ecs_world_view_t(world, GAppPlatformView));
  if (!platformItr) {
    gapp_platform_create(world);
    return;
  }
  GAppPlatformComp* platform = ecs_view_write_t(platformItr, GAppPlatformComp);
  (void)platform;

  EcsView* windowView = ecs_world_view_t(world, GAppWindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {

    const EcsEntityId windowEntity = ecs_view_entity(itr);
    GAppWindowComp*   window       = ecs_view_write_t(itr, GAppWindowComp);
    window_update(world, window, windowEntity);
  }
}

ecs_module_init(gapp_window_module) {
  ecs_register_comp(GAppWindowComp);

  ecs_register_view(GAppPlatformView);
  ecs_register_view(GAppWindowView);

  ecs_register_system(GAppUpdateSys, ecs_view_id(GAppPlatformView), ecs_view_id(GAppWindowView));
}

EcsEntityId gapp_window_open(EcsWorld* world, const GAppWindowFlags flags) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world, windowEntity, GAppWindowComp, .flags = flags, .requests = GAppWindowRequests_Create);
  return windowEntity;
}

void gapp_window_close(GAppWindowComp* window) { window->requests |= GAppWindowRequests_Close; }

GAppWindowEvents gapp_window_events(const GAppWindowComp* window) { return window->events; }
