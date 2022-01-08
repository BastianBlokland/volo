#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "renderer_internal.h"
#include "swapchain_internal.h"

typedef RvkRenderer* RvkRendererPtr;

typedef enum {
  RvkCanvasFlags_Active = 1 << 0,
} RvkCanvasFlags;

struct sRvkCanvas {
  RvkDevice*      device;
  RvkSwapchain*   swapchain;
  RvkCanvasFlags  flags;
  RvkRenderer*    renderers[2];
  u32             rendererIdx;
  RvkSwapchainIdx swapchainIdx;
};

RvkCanvas* rvk_canvas_create(RvkDevice* dev, const GapWindowComp* window) {
  RvkSwapchain* swapchain = rvk_swapchain_create(dev, window);
  RvkCanvas*    canvas    = alloc_alloc_t(g_alloc_heap, RvkCanvas);
  *canvas                 = (RvkCanvas){
      .device       = dev,
      .swapchain    = swapchain,
      .renderers[0] = rvk_renderer_create(dev, 0),
      .renderers[1] = rvk_renderer_create(dev, 1),
  };

  log_d(
      "Vulkan canvas created",
      log_param("size", gap_vector_fmt(gap_window_param(window, GapParam_WindowSize))));

  return canvas;
}

void rvk_canvas_destroy(RvkCanvas* canvas) {
  rvk_device_wait_idle(canvas->device);

  array_for_t(canvas->renderers, RvkRendererPtr, rend) { rvk_renderer_destroy(*rend); }
  rvk_swapchain_destroy(canvas->swapchain);

  log_d("Vulkan canvas destroyed");

  alloc_free_t(g_alloc_heap, canvas);
}

RvkRenderStats rvk_canvas_stats(const RvkCanvas* canvas) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  return rvk_renderer_stats(renderer);
}

bool rvk_canvas_begin(RvkCanvas* canvas, const RendSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  const VkSemaphore beginSemaphore = rvk_renderer_semaphore_begin(renderer);
  canvas->swapchainIdx             = rvk_swapchain_acquire(canvas->swapchain, beginSemaphore, size);
  if (sentinel_check(canvas->swapchainIdx)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  RvkImage* targetImage = rvk_swapchain_image(canvas->swapchain, canvas->swapchainIdx);
  rvk_renderer_begin(renderer, targetImage, RvkImagePhase_Present);
  return true;
}

RvkPass* rvk_canvas_pass_forward(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  return rvk_renderer_pass_forward(renderer);
}

void rvk_canvas_end(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  rvk_renderer_end(renderer);

  const VkSemaphore imageDoneSemaphore = rvk_renderer_semaphore_done(renderer);
  rvk_swapchain_present(canvas->swapchain, imageDoneSemaphore, canvas->swapchainIdx);

  canvas->swapchainIdx = sentinel_u32;
  canvas->rendererIdx ^= 1;
  canvas->flags &= ~RvkCanvasFlags_Active;
}
