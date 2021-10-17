#include "core_sentinel.h"
#include "core_signal.h"
#include "ecs_world.h"
#include "gap_window.h"

#include "pal_internal.h"

typedef enum {
  GapWindowRequests_Create      = 1 << 0,
  GapWindowRequests_Close       = 1 << 1,
  GapWindowRequests_Resize      = 1 << 2,
  GapWindowRequests_UpdateTitle = 1 << 3,
} GapWindowRequests;

ecs_comp_define(GapPlatformComp) { GapPal* pal; };

ecs_comp_define(GapWindowComp) {
  String            title;
  GapWindowId       id;
  GapWindowEvents   events : 16;
  GapWindowFlags    flags : 8;
  GapWindowRequests requests : 8;
  GapWindowMode     mode : 8;
  GapKeySet         keysPressed, keysReleased, keysDown;
  GapVector         params[GapParam_Count];
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

#define copy_param(_P_) window->params[_P_] = gap_pal_window_param(platform->pal, window->id, _P_)

  // Clear the events of the previous tick.
  window->events = 0;

  if (window->requests & GapWindowRequests_Create) {
    window->id = gap_pal_window_create(platform->pal, window->params[GapParam_WindowSize]);
  }
  if (window->requests & GapWindowRequests_UpdateTitle) {
    gap_pal_window_title_set(platform->pal, window->id, window->title);
    window->events |= GapWindowEvents_TitleUpdated;
  }
  if (window->requests & GapWindowRequests_Resize) {
    const bool fullscreen = window->mode == GapWindowMode_Fullscreen;
    gap_pal_window_resize(
        platform->pal, window->id, window->params[GapParam_WindowSize], fullscreen);
  }

  const GapPalWindowFlags palFlags = gap_pal_window_flags(platform->pal, window->id);
  if (palFlags & GapPalWindowFlags_CloseRequested) {
    window->events |= GapWindowEvents_CloseRequested;
  }
  if (palFlags & GapPalWindowFlags_Resized) {
    copy_param(GapParam_WindowSize);
    window->events |= GapWindowEvents_Resized;
  }
  if (palFlags & GapPalWindowFlags_CursorMoved) {
    copy_param(GapParam_CursorPos);
  }
  if (palFlags & GapPalWindowFlags_Scrolled) {
    copy_param(GapParam_ScrollDelta);
  } else {
    window->params[GapParam_ScrollDelta] = gap_vector(0, 0);
  }

  if (palFlags & GapPalWindowFlags_KeyPressed) {
    window->keysPressed = *gap_pal_window_keys_pressed(platform->pal, window->id);
    window->keysDown    = *gap_pal_window_keys_down(platform->pal, window->id);
    window->events |= GapWindowEvents_KeyPressed;
  } else {
    gap_keyset_clear(&window->keysPressed);
  }
  if (palFlags & GapPalWindowFlags_KeyReleased) {
    window->keysReleased = *gap_pal_window_keys_released(platform->pal, window->id);
    window->keysDown     = *gap_pal_window_keys_down(platform->pal, window->id);
    window->events |= GapWindowEvents_KeyReleased;
  } else {
    gap_keyset_clear(&window->keysReleased);
  }

  if (window_should_close(window)) {
    gap_pal_window_destroy(platform->pal, window->id);
    window->events |= GapWindowEvents_Closed;
    ecs_world_entity_destroy(world, windowEntity);
  }

  // All requests have been handled.
  window->requests = 0;

#undef copy_param
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

EcsEntityId gap_window_open(EcsWorld* world, const GapWindowFlags flags, const GapVector size) {
  const EcsEntityId windowEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      windowEntity,
      GapWindowComp,
      .id                          = sentinel_u32,
      .flags                       = flags,
      .requests                    = GapWindowRequests_Create,
      .params[GapParam_WindowSize] = size);
  return windowEntity;
}

void gap_window_close(GapWindowComp* window) { window->requests |= GapWindowRequests_Close; }

GapWindowEvents gap_window_events(const GapWindowComp* window) { return window->events; }

GapWindowMode gap_window_mode(const GapWindowComp* window) { return window->mode; }

void gap_window_resize(GapWindowComp* comp, const GapVector size, const GapWindowMode mode) {
  comp->params[GapParam_WindowSize] = size;
  comp->mode                        = mode;
  comp->requests |= GapWindowRequests_Resize;
}

String gap_window_title_get(const GapWindowComp* window) { return window->title; }

void gap_window_title_set(GapWindowComp* window, const String newTitle) {
  if (!string_is_empty(window->title)) {
    string_free(g_alloc_heap, window->title);
  }
  window->title = string_is_empty(newTitle) ? string_empty : string_dup(g_alloc_heap, newTitle);
  window->requests |= GapWindowRequests_UpdateTitle;
}

GapVector gap_window_param(const GapWindowComp* comp, const GapParam param) {
  return comp->params[param];
}

bool gap_window_key_pressed(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysPressed, key);
}

bool gap_window_key_released(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysReleased, key);
}

bool gap_window_key_down(const GapWindowComp* comp, const GapKey key) {
  return gap_keyset_test(&comp->keysDown, key);
}
