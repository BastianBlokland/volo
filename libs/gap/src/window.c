#include "core_sentinel.h"
#include "core_signal.h"
#include "ecs_world.h"
#include "gap_window.h"

#include "pal_internal.h"

typedef enum {
  GapWindowRequests_None        = 0,
  GapWindowRequests_Create      = 1 << 0,
  GapWindowRequests_Close       = 1 << 1,
  GapWindowRequests_UpdateTitle = 1 << 2,
} GapWindowRequests;

ecs_comp_define(GapPlatformComp) { GapPal* pal; };

ecs_comp_define(GapWindowComp) {
  GapWindowId       id;
  u32               width, height;
  String            title;
  GapWindowFlags    flags;
  GapWindowEvents   events;
  GapWindowRequests requests;
};

static void ecs_destruct_platform_comp(void* data) {
  GapPlatformComp* comp = data;
  gap_pal_destroy(comp->pal);
}

static void ecs_destruct_window_comp(void* data) {
  GapWindowComp* comp = data;
  if (!string_is_empty(comp->title)) {
    string_free(g_alloc_heap, comp->title);
  }
}

static bool window_should_close(GapWindowComp* window) {
  if (window->requests & GapWindowRequests_Close) {
    return true;
  }
  if (window->flags & GapWindowFlags_CloseOnInterupt && signal_is_received(Signal_Interupt)) {
    return true;
  }
  if (window->flags & GapWindowFlags_CloseOnRequest &&
      window->events & GapWindowEvents_CloseRequested) {
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
    window->id = gap_pal_window_create(platform->pal, window->width, window->height);
  }
  if (window->requests & GapWindowRequests_UpdateTitle) {
    gap_pal_window_title_set(platform->pal, window->id, window->title);
    window->events |= GapWindowEvents_TitleUpdated;
  }

  const GapPalWindowFlags palFlags = gap_pal_window_flags(platform->pal, window->id);
  if (palFlags & GapPalWindowFlags_CloseRequested) {
    window->events |= GapWindowEvents_CloseRequested;
  }
  if (palFlags & GapPalWindowFlags_Resized) {
    window->width  = gap_pal_window_width(platform->pal, window->id);
    window->height = gap_pal_window_height(platform->pal, window->id);
    window->events |= GapWindowEvents_Resized;
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

  // Retrieve or create the global platform component.
  EcsIterator*     platformItr = ecs_view_itr_first(ecs_world_view_t(world, GapPlatformView));
  GapPlatformComp* platform;
  if (platformItr) {
    platform = ecs_view_write_t(platformItr, GapPlatformComp);
  } else {
    const EcsEntityId platformEntity = ecs_world_entity_create(world);
    platform                         = ecs_world_add_t(
        world, platformEntity, GapPlatformComp, .pal = gap_pal_create(g_alloc_heap));
  }

  // Process all os window events.
  gap_pal_update(platform->pal);

  // Update all windows.
  EcsView* windowView = ecs_world_view_t(world, GapWindowView);
  for (EcsIterator* itr = ecs_view_itr(windowView); ecs_view_walk(itr);) {

    const EcsEntityId windowEntity = ecs_view_entity(itr);
    GapWindowComp*    window       = ecs_view_write_t(itr, GapWindowComp);
    window_update(world, platform, window, windowEntity);
  }
}

ecs_module_init(gap_window_module) {
  ecs_register_comp(GapWindowComp, .destructor = ecs_destruct_window_comp);
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp);

  ecs_register_view(GapPlatformView);
  ecs_register_view(GapWindowView);

  ecs_register_system(GapUpdateSys, ecs_view_id(GapPlatformView), ecs_view_id(GapWindowView));
}

EcsEntityId
gap_window_open(EcsWorld* world, const GapWindowFlags flags, const u32 width, const u32 height) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      windowEntity,
      GapWindowComp,
      .id       = sentinel_u32,
      .width    = width,
      .height   = height,
      .flags    = flags,
      .requests = GapWindowRequests_Create);
  return windowEntity;
}

void gap_window_close(GapWindowComp* window) { window->requests |= GapWindowRequests_Close; }

GapWindowEvents gap_window_events(const GapWindowComp* window) { return window->events; }

String gap_window_title_get(GapWindowComp* window) { return window->title; }

void gap_window_title_set(GapWindowComp* window, const String newTitle) {
  if (!string_is_empty(window->title)) {
    string_free(g_alloc_heap, window->title);
  }
  window->title = string_is_empty(newTitle) ? string_empty : string_dup(g_alloc_heap, newTitle);
  window->requests |= GapWindowRequests_UpdateTitle;
}

u32 gap_window_width(const GapWindowComp* comp) { return comp->width; }
u32 gap_window_height(const GapWindowComp* comp) { return comp->height; }
