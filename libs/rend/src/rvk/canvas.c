#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "renderer_internal.h"

typedef RvkRenderer* RvkRendererPtr;

RvkCanvas* rvk_canvas_create(RvkDevice* dev, const GapWindowComp* window) {

  RvkSwapchain* swapchain = rvk_swapchain_create(dev, window);
  RvkCanvas*    canvas    = alloc_alloc_t(g_alloc_heap, RvkCanvas);
  *canvas                 = (RvkCanvas){
      .device       = dev,
      .swapchain    = swapchain,
      .renderers[0] = rvk_renderer_create(dev, swapchain),
      .renderers[1] = rvk_renderer_create(dev, swapchain),
  };
  return canvas;
}

void rvk_canvas_destroy(RvkCanvas* canvas) {

  rvk_device_wait_idle(canvas->device);

  array_for_t(canvas->renderers, RvkRendererPtr, rend) { rvk_renderer_destroy(*rend); }

  rvk_swapchain_destroy(canvas->swapchain);

  alloc_free_t(g_alloc_heap, canvas);
}

bool rvk_canvas_draw_begin(RvkCanvas* canvas, const RendSize size, const RendColor clearColor) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  const VkSemaphore beginSemaphore = rvk_renderer_semaphore_begin(renderer);
  canvas->swapchainIdx             = rvk_swapchain_acquire(canvas->swapchain, beginSemaphore, size);
  if (sentinel_check(canvas->swapchainIdx)) {
    return false;
  }

  rvk_renderer_draw_begin(renderer, canvas->swapchainIdx, clearColor);
  return true;
}

void rvk_canvas_draw_inst(RvkCanvas* canvas, RvkGraphic* graphic) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  rvk_renderer_draw_inst(renderer, graphic);
}

void rvk_canvas_draw_end(RvkCanvas* canvas) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  rvk_renderer_draw_end(renderer, canvas->swapchainIdx);

  const VkSemaphore imageDoneSemaphore = rvk_renderer_semaphore_done(renderer);
  rvk_swapchain_present(canvas->swapchain, imageDoneSemaphore, canvas->swapchainIdx);

  canvas->swapchainIdx = sentinel_u32;
  canvas->rendererIdx ^= 1;
}
