#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "attach_internal.h"
#include "canvas_internal.h"
#include "device_internal.h"
#include "renderer_internal.h"
#include "swapchain_internal.h"

typedef RvkRenderer* RvkRendererPtr;

/**
 * Use two renderers for double bufferring:
 * - One being recorded on the cpu.
 * - One being rendered on the gpu.
 */
#define canvas_renderer_count 2

typedef enum {
  RvkCanvasFlags_Active    = 1 << 0,
  RvkCanvasFlags_Submitted = 1 << 1, // Submitted at least once.
} RvkCanvasFlags;

struct sRvkCanvas {
  RvkDevice*      dev;
  RvkSwapchain*   swapchain;
  RvkAttachPool*  attachPool;
  RvkCanvasFlags  flags;
  RvkRenderer*    renderers[canvas_renderer_count];
  VkSemaphore     attachmentsReleased[canvas_renderer_count];
  VkSemaphore     swapchainAvailable[canvas_renderer_count];
  VkSemaphore     swapchainPresent[canvas_renderer_count];
  u32             rendererIdx;
  RvkSwapchainIdx swapchainIdx;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

RvkCanvas* rvk_canvas_create(RvkDevice* dev, const GapWindowComp* window) {
  RvkSwapchain*  swapchain  = rvk_swapchain_create(dev, window);
  RvkAttachPool* attachPool = rvk_attach_pool_create(dev);
  RvkCanvas*     canvas     = alloc_alloc_t(g_alloc_heap, RvkCanvas);

  *canvas = (RvkCanvas){
      .dev        = dev,
      .swapchain  = swapchain,
      .attachPool = attachPool,
  };

  for (u32 i = 0; i != canvas_renderer_count; ++i) {
    canvas->renderers[i]           = rvk_renderer_create(dev, i);
    canvas->attachmentsReleased[i] = rvk_semaphore_create(dev);
    canvas->swapchainAvailable[i]  = rvk_semaphore_create(dev);
    canvas->swapchainPresent[i]    = rvk_semaphore_create(dev);
  }

  log_d(
      "Vulkan canvas created",
      log_param("size", gap_vector_fmt(gap_window_param(window, GapParam_WindowSize))));

  return canvas;
}

void rvk_canvas_destroy(RvkCanvas* canvas) {
  rvk_device_wait_idle(canvas->dev);

  for (u32 i = 0; i != canvas_renderer_count; ++i) {
    rvk_renderer_destroy(canvas->renderers[i]);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->attachmentsReleased[i], &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->swapchainAvailable[i], &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->swapchainPresent[i], &canvas->dev->vkAlloc);
  }

  rvk_swapchain_destroy(canvas->swapchain);
  rvk_attach_pool_destroy(canvas->attachPool);

  log_d("Vulkan canvas destroyed");

  alloc_free_t(g_alloc_heap, canvas);
}

RvkAttachPool* rvk_canvas_attach_pool(RvkCanvas* canvas) { return canvas->attachPool; }
RvkRepository* rvk_canvas_repository(RvkCanvas* canvas) { return canvas->dev->repository; }

RvkRenderStats rvk_canvas_render_stats(const RvkCanvas* canvas) {
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  return rvk_renderer_stats(renderer);
}

RvkSwapchainStats rvk_canvas_swapchain_stats(const RvkCanvas* canvas) {
  return rvk_swapchain_stats(canvas->swapchain);
}

bool rvk_canvas_begin(RvkCanvas* canvas, const RendSettingsComp* settings, const RvkSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  const VkSemaphore availableSema = canvas->swapchainAvailable[canvas->rendererIdx];
  canvas->swapchainIdx = rvk_swapchain_acquire(canvas->swapchain, settings, availableSema, size);
  if (sentinel_check(canvas->swapchainIdx)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  rvk_renderer_begin(renderer);
  return true;
}

RvkPass* rvk_canvas_pass(RvkCanvas* canvas, const RvkRenderPass pass) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  return rvk_renderer_pass(renderer, pass);
}

RvkImage* rvk_canvas_output(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  return rvk_swapchain_image(canvas->swapchain, canvas->swapchainIdx);
}

void rvk_canvas_copy(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  rvk_renderer_copy(renderer, src, dst);
}

void rvk_canvas_blit(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];
  rvk_renderer_blit(renderer, src, dst);
}

void rvk_canvas_end(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkRenderer* renderer = canvas->renderers[canvas->rendererIdx];

  // Transition the swapchain-image to the present phase.
  RvkImage* swapchainImage = rvk_swapchain_image(canvas->swapchain, canvas->swapchainIdx);
  rvk_renderer_transition(renderer, swapchainImage, RvkImagePhase_Present);

  VkSemaphore attachmentsReady = null;
  if (canvas->flags & RvkCanvasFlags_Submitted) {
    /**
     * Wait for the other renderer to release the attachments.
     * Reason is we reuse the attachments in both renderers to avoid wasting gpu memory.
     */
    attachmentsReady = canvas->attachmentsReleased[canvas->rendererIdx ^ 1];
  }
  const VkSemaphore endSignals[] = {
      canvas->swapchainPresent[canvas->rendererIdx],    // Trigger the present.
      canvas->attachmentsReleased[canvas->rendererIdx], // Trigger the next renderer.
  };

  const VkSemaphore swapchainSema = canvas->swapchainAvailable[canvas->rendererIdx];
  rvk_renderer_end(renderer, attachmentsReady, swapchainSema, endSignals, array_elems(endSignals));

  rvk_swapchain_enqueue_present(
      canvas->swapchain, canvas->swapchainPresent[canvas->rendererIdx], canvas->swapchainIdx);

  rvk_attach_pool_flush(canvas->attachPool);

  canvas->swapchainIdx = sentinel_u32;
  canvas->rendererIdx ^= 1;
  canvas->flags |= RvkCanvasFlags_Submitted;
  canvas->flags &= ~RvkCanvasFlags_Active;
}

void rvk_canvas_wait_for_prev_present(const RvkCanvas* canvas) {
  const u32 numBehind = 1;
  rvk_swapchain_wait_for_present(canvas->swapchain, numBehind);
}
