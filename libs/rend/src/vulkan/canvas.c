#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "canvas_internal.h"

RendVkCanvas* rend_vk_canvas_create(RendVkDevice* dev, const GapWindowComp* window) {
  RendVkCanvas* canvas = alloc_alloc_t(g_alloc_heap, RendVkCanvas);
  *canvas              = (RendVkCanvas){
      .device    = dev,
      .swapchain = rend_vk_swapchain_create(dev, window),
  };
  return canvas;
}

void rend_vk_canvas_destroy(RendVkCanvas* canvas) {
  // Wait for device activity be done before destroying the surface.
  rend_vk_call(vkDeviceWaitIdle, canvas->device->vkDevice);

  rend_vk_swapchain_destroy(canvas->swapchain);

  alloc_free_t(g_alloc_heap, canvas);
}

void rend_vk_canvas_resize(RendVkCanvas* canvas, const GapVector size) {
  (void)canvas;
  (void)size;

  log_i("Vulkan canvas resized", log_param("size", gap_vector_fmt(size)));
}
