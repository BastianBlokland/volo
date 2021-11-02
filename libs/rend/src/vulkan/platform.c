#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "platform_internal.h"

typedef struct {
  RendVkCanvasId id;
  RendVkCanvas*  canvas;
} RendVkCanvasInfo;

struct sRendVkPlatform {
  RendVkDevice* device;
  DynArray      canvases; // RendVkCanvasInfo[]
};

static RendVkCanvas* rend_vk_canvas_lookup(RendVkPlatform* plat, const RendVkCanvasId id) {
  dynarray_for_t(&plat->canvases, RendVkCanvasInfo, info, {
    if (info->id == id) {
      return info->canvas;
    }
  });
  diag_crash_msg("No canvas found with id: {}", fmt_int(id));
}

RendVkPlatform* rend_vk_platform_create() {
  RendVkPlatform* plat = alloc_alloc_t(g_alloc_heap, RendVkPlatform);
  *plat                = (RendVkPlatform){
      .device   = rend_vk_device_create(),
      .canvases = dynarray_create_t(g_alloc_heap, RendVkCanvasInfo, 4),
  };
  return plat;
}

void rend_vk_platform_destroy(RendVkPlatform* plat) {
  while (plat->canvases.size) {
    rend_vk_platform_canvas_destroy(plat, dynarray_at_t(&plat->canvases, 0, RendVkCanvasInfo)->id);
  }

  rend_vk_device_destroy(plat->device);

  dynarray_destroy(&plat->canvases);
  alloc_free_t(g_alloc_heap, plat);
}

RendVkCanvasId rend_vk_platform_canvas_create(RendVkPlatform* plat, const GapWindowComp* window) {
  static i64 nextCanvasId = 0;

  RendVkCanvasId id = (RendVkCanvasId)thread_atomic_add_i64(&nextCanvasId, 1);
  *dynarray_push_t(&plat->canvases, RendVkCanvasInfo) = (RendVkCanvasInfo){
      .id     = id,
      .canvas = rend_vk_canvas_create(plat->device, window),
  };
  return id;
}

void rend_vk_platform_canvas_destroy(RendVkPlatform* plat, const RendVkCanvasId id) {
  for (usize i = 0; i != plat->canvases.size; ++i) {
    RendVkCanvasInfo* canvasInfo = dynarray_at_t(&plat->canvases, i, RendVkCanvasInfo);
    if (canvasInfo->id == id) {
      rend_vk_canvas_destroy(canvasInfo->canvas);
      dynarray_remove_unordered(&plat->canvases, i, 1);
      break;
    }
  }
}

bool rend_vk_platform_draw_begin(
    RendVkPlatform*      plat,
    const RendVkCanvasId id,
    const RendSize       size,
    const RendColor      clearColor) {
  return rend_vk_canvas_draw_begin(rend_vk_canvas_lookup(plat, id), size, clearColor);
}

void rend_vk_platform_draw_end(RendVkPlatform* plat, const RendVkCanvasId id) {
  rend_vk_canvas_draw_end(rend_vk_canvas_lookup(plat, id));
}
