#include "core_alloc.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_input.h"
#include "gap_window.h"
#include "rend_canvas.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"

typedef enum {
  RendCanvasRequests_Create = 1 << 0,
} RendCanvasRequests;

typedef struct {
  RvkGraphic* rvkGraphic;
} RendDrawEntry;

ecs_comp_define(RendCanvasComp) {
  RvkCanvas*         canvas;
  RendCanvasRequests requests;
  RendColor          clearColor;
  DynArray           drawList; // RendDrawEntry[]
};

static void ecs_destruct_canvas_comp(void* data) {
  RendCanvasComp* comp = data;
  if (comp->canvas) {
    rvk_canvas_destroy(comp->canvas);
  }
  dynarray_destroy(&comp->drawList);
}

ecs_view_define(PlatformView) { ecs_access_write(RendPlatformComp); };

ecs_view_define(RenderableView) { ecs_access_read(RendGraphicComp); };

ecs_view_define(CanvasView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(RendCanvasComp);
};

static void canvas_update(EcsWorld* world, RendCanvasComp* canvas) {
  dynarray_clear(&canvas->drawList);

  EcsView* renderableView = ecs_world_view_t(world, RenderableView);
  for (EcsIterator* itr = ecs_view_itr(renderableView); ecs_view_walk(itr);) {
    const RendGraphicComp* graphicComp = ecs_view_read_t(itr, RendGraphicComp);

    RendDrawEntry* entry = dynarray_push_t(&canvas->drawList, RendDrawEntry);
    *entry               = (RendDrawEntry){.rvkGraphic = graphicComp->graphic};
  }
}

static bool canvas_draw(RendCanvasComp* canvas, const GapWindowComp* win) {

  const GapVector winSize  = gap_window_param(win, GapParam_WindowSize);
  const RendSize  rendSize = rend_size((u32)winSize.width, (u32)winSize.height);
  const bool      draw     = rvk_canvas_draw_begin(canvas->canvas, rendSize, canvas->clearColor);
  if (draw) {
    dynarray_for_t(&canvas->drawList, RendDrawEntry, entry) {
      rvk_canvas_draw_inst(canvas->canvas, entry->rvkGraphic);
    }
    rvk_canvas_draw_end(canvas->canvas);
  }

  return draw;
}

ecs_system_define(RendCanvasUpdateSys) {
  RendPlatformComp* plat = ecs_utils_write_first_t(world, PlatformView, RendPlatformComp);
  if (!plat) {
    return;
  }

  EcsView* canvasView = ecs_world_view_t(world, CanvasView);

  bool anyCanvasDrawn = false;
  for (EcsIterator* itr = ecs_view_itr(canvasView); ecs_view_walk(itr);) {
    const GapWindowComp* win       = ecs_view_read_t(itr, GapWindowComp);
    RendCanvasComp*      canvas    = ecs_view_write_t(itr, RendCanvasComp);
    GapWindowEvents      winEvents = gap_window_events(win);

    if (canvas->requests & RendCanvasRequests_Create) {
      canvas->canvas = rvk_canvas_create(plat->device, win);
    }
    if (winEvents & GapWindowEvents_Closed) {
      canvas->requests = 0;
      continue;
    }

    canvas_update(world, canvas);

    anyCanvasDrawn |= canvas_draw(canvas, win);
    canvas->requests = 0;
  }

  if (!anyCanvasDrawn) {
    /**
     * If no canvas was drawn this frame (for example because they are all minimized) we sleep
     * the thread to avoid wasting cpu cycles.
     */
    thread_sleep(time_second / 30);
  }
}

ecs_module_init(rend_canvas_module) {
  ecs_register_comp(RendCanvasComp, .destructor = ecs_destruct_canvas_comp);

  ecs_register_view(PlatformView);
  ecs_register_view(RenderableView);
  ecs_register_view(CanvasView);

  ecs_register_system(
      RendCanvasUpdateSys,
      ecs_view_id(PlatformView),
      ecs_view_id(RenderableView),
      ecs_view_id(CanvasView));
}

void rend_canvas_create(
    EcsWorld* world, const EcsEntityId windowEntity, const RendColor clearColor) {
  ecs_world_add_t(
      world,
      windowEntity,
      RendCanvasComp,
      .requests   = RendCanvasRequests_Create,
      .clearColor = clearColor,
      .drawList   = dynarray_create_t(g_alloc_heap, RendDrawEntry, 1024));
}

void rend_canvas_teardown(EcsWorld* world) {

  EcsView* canvasView = ecs_world_view_t(world, CanvasView);
  for (EcsIterator* itr = ecs_view_itr(canvasView); ecs_view_walk(itr);) {
    RendCanvasComp* comp = ecs_view_write_t(itr, RendCanvasComp);
    if (comp->canvas) {
      rvk_canvas_destroy(comp->canvas);
      comp->canvas = null;
    }
  }
}
