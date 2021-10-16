#include "core_signal.h"
#include "ecs_world.h"
#include "gapp_window.h"

typedef enum {
  GappWindowRequests_None   = 0,
  GappWindowRequests_Create = 1 << 0,
  GappWindowRequests_Close  = 1 << 1,
} GappWindowRequests;

ecs_comp_define(GappWindow) {
  GappWindowFlags    flags;
  GappWindowEvents   events;
  GappWindowRequests requests;
};

static bool window_should_close(GappWindow* window) {
  if (window->requests & GappWindowRequests_Close) {
    return true;
  }
  if (window->flags & GappWindowFlags_CloseOnInterupt && signal_is_received(Signal_Interupt)) {
    return true;
  }
  return false;
}

static void window_update(EcsWorld* world, GappWindow* window, const EcsEntityId windowEntity) {
  // Clear the events of the previous tick.
  window->events = GappWindowEvents_None;

  if (window_should_close(window)) {
    window->events |= GappWindowEvents_Closed;
    ecs_world_entity_destroy(world, windowEntity);
  }

  // All requests have been handled.
  window->requests = GappWindowRequests_None;
}

ecs_view_define(WindowView) { ecs_access_write(GappWindow); };

ecs_system_define(UpdateSys) {

  EcsView* windowView = ecs_world_view_t(world, WindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {

    const EcsEntityId windowEntity = ecs_view_entity(itr);
    GappWindow*       window       = ecs_view_write_t(itr, GappWindow);
    window_update(world, window, windowEntity);
  }
}

ecs_module_init(gapp_window_module) {
  ecs_register_comp(GappWindow);

  ecs_register_view(WindowView);

  ecs_register_system(UpdateSys, ecs_view_id(WindowView));
}

EcsEntityId gapp_window_open(EcsWorld* world, const GappWindowFlags flags) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world, windowEntity, GappWindow, .flags = flags, .requests = GappWindowRequests_Create);
  return windowEntity;
}

void gapp_window_close(GappWindow* window) { window->requests |= GappWindowRequests_Close; }

GappWindowEvents gapp_window_events(const GappWindow* window) { return window->events; }
