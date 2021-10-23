#include "ecs_world.h"
#include "gap_window.h"
#include "rend_canvas.h"

#include "platform_internal.h"

typedef enum {
  RendCanvasRequests_Create = 1 << 0,
} RendCanvasRequests;

ecs_comp_define(RendCanvasComp) { RendCanvasRequests requests; };

static void canvas_update(
    EcsWorld*            world,
    RendPlatformComp*    platform,
    const GapWindowComp* window,
    RendCanvasComp*      canvas) {
  (void)world;
  (void)platform;
  (void)window;

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
        world,
        platform,
        ecs_view_read_t(itr, GapWindowComp),
        ecs_view_write_t(itr, RendCanvasComp));
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
