#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "renderer_internal.h"

RvkCanvas* rvk_canvas_create(RvkDevice* dev, const GapWindowComp* window) {

  RvkSwapchain* swapchain = rvk_swapchain_create(dev, window);
  RvkCanvas*    canvas    = alloc_alloc_t(g_alloc_heap, RvkCanvas);
  *canvas                 = (RvkCanvas){
      .device       = dev,
      .swapchain    = swapchain,
      .technique    = rvk_technique_create(dev, swapchain),
      .renderers[0] = rvk_renderer_create(dev, swapchain),
      .renderers[1] = rvk_renderer_create(dev, swapchain),
  };
  return canvas;
}

void rvk_canvas_destroy(RvkCanvas* canvas) {
  // Wait all renderering be done before destroying the surface.
  rvk_call(vkDeviceWaitIdle, canvas->device->vkDev);

  array_for_t(canvas->renderers, RvkRenderer*, rend, { rvk_renderer_destroy(*rend); });

  rvk_technique_destroy(canvas->technique);
  rvk_swapchain_destroy(canvas->swapchain);

  alloc_free_t(g_alloc_heap, canvas);
}

bool rvk_canvas_draw_begin(RvkCanvas* canvas, const RendSize size, const RendColor clearColor) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  canvas->swapchainIdx =
      rvk_swapchain_acquire(canvas->swapchain, rvk_renderer_image_available(renderer), size);
  if (sentinel_check(canvas->swapchainIdx)) {
    return false;
  }

  rvk_renderer_draw_begin(renderer, canvas->technique, canvas->swapchainIdx, clearColor);
  return true;
}

void rvk_canvas_draw_end(RvkCanvas* canvas) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  rvk_renderer_draw_end(renderer, canvas->technique);

  rvk_swapchain_present(
      canvas->swapchain, rvk_renderer_image_ready(renderer), canvas->swapchainIdx);

  canvas->swapchainIdx = sentinel_u32;
  canvas->rendererIdx ^= 1;
}
