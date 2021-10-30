#include "ecs_world.h"
#include "gap_input.h"
#include "gap_window.h"
#include "rend_canvas.h"

#include "platform_internal.h"
#include "vulkan/platform_internal.h"

typedef enum {
  RendCanvasRequests_Create = 1 << 0,
} RendCanvasRequests;

ecs_comp_define(RendCanvasComp) {
  RendVkCanvasId     id;
  RendCanvasRequests requests;
};

static void
canvas_update(RendPlatformComp* platform, const GapWindowComp* window, RendCanvasComp* canvas) {
  GapWindowEvents winEvents = gap_window_events(window);

  if (canvas->requests & RendCanvasRequests_Create) {
    const GapVector winSize = gap_window_param(window, GapParam_WindowSize);
    canvas->id              = rend_vk_platform_canvas_create(platform->vulkan, winSize);
  }
  if (winEvents & GapWindowEvents_Resized) {
    const GapVector winSize = gap_window_param(window, GapParam_WindowSize);
    rend_vk_platform_canvas_resize(platform->vulkan, canvas->id, winSize);
  }
  if (winEvents & GapWindowEvents_Closed) {
    rend_vk_platform_canvas_destroy(platform->vulkan, canvas->id);
  }

  // All requests have been handled.
  canvas->requests = 0;
}

ecs_view_define(RendPlatformView) { ecs_access_write(RendPlatformComp); };

ecs_view_define(RendCanvasView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(RendCanvasComp);
};

static RendPlatformComp* rend_platform_get(EcsWorld* world) {
  EcsIterator* itr = ecs_view_itr_first(ecs_world_view_t(world, RendPlatformView));
  return itr ? ecs_view_write_t(itr, RendPlatformComp) : null;
}

ecs_system_define(RendCanvasUpdateSys) {
  RendPlatformComp* platform = rend_platform_get(world);
  if (!platform) {
    return;
  }

  EcsView* canvasView = ecs_world_view_t(world, RendCanvasView);
  for (EcsIterator* itr = ecs_view_itr(canvasView); ecs_view_walk(itr);) {
    canvas_update(
        platform, ecs_view_read_t(itr, GapWindowComp), ecs_view_write_t(itr, RendCanvasComp));
  }
}

ecs_module_init(rend_canvas_module) {
  ecs_register_comp(RendCanvasComp);

  ecs_register_view(RendPlatformView);
  ecs_register_view(RendCanvasView);

  ecs_register_system(
      RendCanvasUpdateSys, ecs_view_id(RendPlatformView), ecs_view_id(RendCanvasView));
}

void rend_canvas_create(EcsWorld* world, EcsEntityId windowEntity) {
  ecs_world_add_t(world, windowEntity, RendCanvasComp, .requests = RendCanvasRequests_Create);
}
