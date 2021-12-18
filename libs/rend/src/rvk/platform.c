#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "platform_internal.h"

typedef struct {
  RvkCanvasId id;
  RvkCanvas*  canvas;
} RvkCanvasInfo;

struct sRvkPlatform {
  RvkDevice* dev;
  DynArray   canvases; // RvkCanvasInfo[]
};

static RvkCanvas* rvk_canvas_lookup(RvkPlatform* plat, const RvkCanvasId id) {
  dynarray_for_t(&plat->canvases, RvkCanvasInfo, info) {
    if (info->id == id) {
      return info->canvas;
    }
  }
  diag_crash_msg("No canvas found with id: {}", fmt_int(id));
}

RvkPlatform* rvk_platform_create() {
  RvkPlatform* plat = alloc_alloc_t(g_alloc_heap, RvkPlatform);
  *plat             = (RvkPlatform){
      .dev      = rvk_device_create(),
      .canvases = dynarray_create_t(g_alloc_heap, RvkCanvasInfo, 4),
  };
  return plat;
}

void rvk_platform_destroy(RvkPlatform* plat) {
  while (plat->canvases.size) {
    rvk_platform_canvas_destroy(plat, dynarray_at_t(&plat->canvases, 0, RvkCanvasInfo)->id);
  }

  rvk_device_destroy(plat->dev);

  dynarray_destroy(&plat->canvases);
  alloc_free_t(g_alloc_heap, plat);
}

RvkDevice* rvk_platform_device(const RvkPlatform* plat) { return plat->dev; }

void rvk_platform_update(RvkPlatform* plat) { rvk_device_update(plat->dev); }

void rvk_platform_wait_idle(const RvkPlatform* plat) { rvk_device_wait_idle(plat->dev); }

RvkCanvasId rvk_platform_canvas_create(RvkPlatform* plat, const GapWindowComp* window) {
  static i64 nextCanvasId = 0;

  RvkCanvasId id = (RvkCanvasId)thread_atomic_add_i64(&nextCanvasId, 1);
  *dynarray_push_t(&plat->canvases, RvkCanvasInfo) = (RvkCanvasInfo){
      .id     = id,
      .canvas = rvk_canvas_create(plat->dev, window),
  };
  return id;
}

void rvk_platform_canvas_destroy(RvkPlatform* plat, const RvkCanvasId id) {
  for (usize i = 0; i != plat->canvases.size; ++i) {
    RvkCanvasInfo* canvasInfo = dynarray_at_t(&plat->canvases, i, RvkCanvasInfo);
    if (canvasInfo->id == id) {
      rvk_canvas_destroy(canvasInfo->canvas);
      dynarray_remove_unordered(&plat->canvases, i, 1);
      break;
    }
  }
}

bool rvk_platform_prepare_graphic(RvkPlatform* plat, const RvkCanvasId id, RvkGraphic* graphic) {
  return rvk_graphic_prepare(graphic, rvk_canvas_lookup(plat, id));
}

bool rvk_platform_draw_begin(
    RvkPlatform* plat, const RvkCanvasId id, const RendSize size, const RendColor clearColor) {
  return rvk_canvas_draw_begin(rvk_canvas_lookup(plat, id), size, clearColor);
}

void rvk_platform_draw_inst(RvkPlatform* plat, const RvkCanvasId id, RvkGraphic* graphic) {
  rvk_canvas_draw_inst(rvk_canvas_lookup(plat, id), graphic);
}

void rvk_platform_draw_end(RvkPlatform* plat, const RvkCanvasId id) {
  rvk_canvas_draw_end(rvk_canvas_lookup(plat, id));
}
