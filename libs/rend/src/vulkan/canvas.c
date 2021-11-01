#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "renderer_internal.h"

RendVkCanvas* rend_vk_canvas_create(RendVkDevice* dev, const GapWindowComp* window) {

  RendVkSwapchain* swapchain = rend_vk_swapchain_create(dev, window);
  RendVkCanvas*    canvas    = alloc_alloc_t(g_alloc_heap, RendVkCanvas);
  *canvas                    = (RendVkCanvas){
      .device       = dev,
      .swapchain    = swapchain,
      .technique    = rend_vk_technique_create(dev, swapchain),
      .renderers[0] = rend_vk_renderer_create(dev, swapchain),
      .renderers[1] = rend_vk_renderer_create(dev, swapchain),
  };
  return canvas;
}

void rend_vk_canvas_destroy(RendVkCanvas* canvas) {
  // Wait all renderering be done before destroying the surface.
  rend_vk_call(vkDeviceWaitIdle, canvas->device->vkDevice);

  array_for_t(canvas->renderers, RendVkRenderer*, rend, { rend_vk_renderer_destroy(*rend); });

  rend_vk_technique_destroy(canvas->technique);
  rend_vk_swapchain_destroy(canvas->swapchain);

  alloc_free_t(g_alloc_heap, canvas);
}

bool rend_vk_canvas_draw_begin(
    RendVkCanvas* canvas, const RendSize size, const RendColor clearColor) {
  RendVkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  canvas->curSwapchainIdx = rend_vk_swapchain_acquire(
      canvas->swapchain, rend_vk_renderer_image_available(renderer), size);
  if (sentinel_check(canvas->curSwapchainIdx)) {
    return false;
  }

  rend_vk_renderer_draw_begin(renderer, canvas->technique, canvas->curSwapchainIdx, clearColor);
  return true;
}

void rend_vk_canvas_draw_end(RendVkCanvas* canvas) {
  RendVkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  rend_vk_renderer_draw_end(renderer, canvas->technique);

  rend_vk_swapchain_present(
      canvas->swapchain, rend_vk_renderer_image_ready(renderer), canvas->curSwapchainIdx);

  canvas->curSwapchainIdx = sentinel_u32;
  canvas->rendererIdx ^= 1;
}
