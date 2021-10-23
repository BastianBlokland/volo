#include "core_diag.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_canvas.h"

typedef enum {
  RendCanvasRequests_Create = 1 << 0,
} RendCanvasRequests;

ecs_comp_define(RendCanvasComp) { RendCanvasRequests requests; };

static void ecs_destruct_canvas_comp(void* data) { (void)data; }

ecs_view_define(RendCanvasView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(RendCanvasComp);
};

static void canvas_update(EcsWorld* world, const GapWindowComp* window, RendCanvasComp* canvas) {
  (void)world;
  (void)window;

  // All requests have been handled.
  canvas->requests = 0;
}

ecs_system_define(RendCanvasUpdateSys) {
  EcsView* canvasView = ecs_world_view_t(world, RendCanvasView);
  for (EcsIterator* itr = ecs_view_itr(canvasView); ecs_view_walk(itr);) {

    const GapWindowComp* window = ecs_view_read_t(itr, GapWindowComp);
    RendCanvasComp*      canvas = ecs_view_write_t(itr, RendCanvasComp);
    canvas_update(world, window, canvas);
  }
}

ecs_module_init(rend_canvas_module) {
  ecs_register_comp(RendCanvasComp, .destructor = ecs_destruct_canvas_comp);

  ecs_register_view(RendCanvasView);

  ecs_register_system(RendCanvasUpdateSys, ecs_view_id(RendCanvasView));
}

void rend_canvas_create(EcsWorld* world, EcsEntityId windowEntity) {
  ecs_world_add_t(world, windowEntity, RendCanvasComp, .requests = RendCanvasRequests_Create);
}
