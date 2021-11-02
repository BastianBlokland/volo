#include "core_thread.h"
#include "core_time.h"
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
  RendColor          clearColor;
};

static bool
canvas_render(RendPlatformComp* plat, const GapWindowComp* window, RendCanvasComp* canvas) {
  GapWindowEvents winEvents = gap_window_events(window);

  if (canvas->requests & RendCanvasRequests_Create) {
    canvas->id = rend_vk_platform_canvas_create(plat->vulkan, window);
  }
  if (winEvents & GapWindowEvents_Closed) {
    rend_vk_platform_canvas_destroy(plat->vulkan, canvas->id);
    canvas->requests = 0;
    return false;
  }

  const GapVector winSize  = gap_window_param(window, GapParam_WindowSize);
  const RendSize  rendSize = rend_size((u32)winSize.width, (u32)winSize.height);
  const bool      draw =
      rend_vk_platform_draw_begin(plat->vulkan, canvas->id, rendSize, canvas->clearColor);
  if (draw) {
    rend_vk_platform_draw_end(plat->vulkan, canvas->id);
  }

  canvas->requests = 0;
  return draw;
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

  bool anyCanvasRendered = false;
  for (EcsIterator* itr = ecs_view_itr(canvasView); ecs_view_walk(itr);) {
    anyCanvasRendered |= canvas_render(
        platform, ecs_view_read_t(itr, GapWindowComp), ecs_view_write_t(itr, RendCanvasComp));
  }

  if (!anyCanvasRendered) {
    /**
     * If no canvas was rendered this frame (for example because they are all minimized) we sleep
     * the thread to avoid wasting cpu cycles.
     */
    thread_sleep(time_second / 30);
  }
}

ecs_module_init(rend_canvas_module) {
  ecs_register_comp(RendCanvasComp);

  ecs_register_view(RendPlatformView);
  ecs_register_view(RendCanvasView);

  ecs_register_system(
      RendCanvasUpdateSys, ecs_view_id(RendPlatformView), ecs_view_id(RendCanvasView));
}

void rend_canvas_create(
    EcsWorld* world, const EcsEntityId windowEntity, const RendColor clearColor) {
  ecs_world_add_t(
      world,
      windowEntity,
      RendCanvasComp,
      .requests   = RendCanvasRequests_Create,
      .clearColor = clearColor);
}
