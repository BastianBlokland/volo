#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "attach_internal.h"
#include "canvas_internal.h"
#include "device_internal.h"
#include "job_internal.h"
#include "pass_internal.h"
#include "swapchain_internal.h"

typedef RvkJob* RvkJobPtr;

/**
 * Use two jobs for double bufferring:
 * - One being recorded on the cpu.
 * - One being rendered on the gpu.
 */
#define canvas_job_count 2

typedef enum {
  RvkCanvasFlags_Active    = 1 << 0,
  RvkCanvasFlags_Submitted = 1 << 1, // Submitted at least once.
} RvkCanvasFlags;

struct sRvkCanvas {
  RvkDevice*      dev;
  RvkSwapchain*   swapchain;
  RvkAttachPool*  attachPool;
  RvkCanvasFlags  flags;
  RvkJob*         jobs[canvas_job_count];
  VkSemaphore     attachmentsReleased[canvas_job_count];
  VkSemaphore     swapchainAvailable[canvas_job_count];
  VkSemaphore     swapchainPresent[canvas_job_count];
  u32             jobIdx;
  RvkSwapchainIdx swapchainIdx;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

RvkCanvas* rvk_canvas_create(
    RvkDevice*           dev,
    const GapWindowComp* window,
    const RvkPassConfig* passConfig /* RvkPassConfig[RendPass_Count] */) {
  RvkSwapchain*  swapchain       = rvk_swapchain_create(dev, window);
  const VkFormat swapchainFormat = rvk_swapchain_format(swapchain);
  RvkAttachPool* attachPool      = rvk_attach_pool_create(dev);
  RvkCanvas*     canvas          = alloc_alloc_t(g_alloc_heap, RvkCanvas);

  *canvas = (RvkCanvas){
      .dev        = dev,
      .swapchain  = swapchain,
      .attachPool = attachPool,
  };

  for (u32 i = 0; i != canvas_job_count; ++i) {
    canvas->jobs[i]                = rvk_job_create(dev, swapchainFormat, i, passConfig);
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

  for (u32 i = 0; i != canvas_job_count; ++i) {
    rvk_job_destroy(canvas->jobs[i]);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->attachmentsReleased[i], &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->swapchainAvailable[i], &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, canvas->swapchainPresent[i], &canvas->dev->vkAlloc);
  }

  rvk_swapchain_destroy(canvas->swapchain);
  rvk_attach_pool_destroy(canvas->attachPool);

  log_d("Vulkan canvas destroyed");

  alloc_free_t(g_alloc_heap, canvas);
}

RvkRepository* rvk_canvas_repository(RvkCanvas* canvas) { return canvas->dev->repository; }

RvkCanvasStats rvk_canvas_stats(const RvkCanvas* canvas) {
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  return rvk_job_stats(job);
}

u16 rvk_canvas_attach_count(const RvkCanvas* canvas) {
  return rvk_attach_pool_count(canvas->attachPool);
}

u64 rvk_canvas_attach_memory(const RvkCanvas* canvas) {
  return rvk_attach_pool_memory(canvas->attachPool);
}

RvkSwapchainStats rvk_canvas_swapchain_stats(const RvkCanvas* canvas) {
  return rvk_swapchain_stats(canvas->swapchain);
}

bool rvk_canvas_begin(RvkCanvas* canvas, const RendSettingsComp* settings, const RvkSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkJob* job = canvas->jobs[canvas->jobIdx];

  const VkSemaphore availableSema = canvas->swapchainAvailable[canvas->jobIdx];
  canvas->swapchainIdx = rvk_swapchain_acquire(canvas->swapchain, settings, availableSema, size);
  if (sentinel_check(canvas->swapchainIdx)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  rvk_job_begin(job);
  return true;
}

RvkPass* rvk_canvas_pass(RvkCanvas* canvas, const RendPass pass) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  return rvk_job_pass(job, pass);
}

RvkImage* rvk_canvas_swapchain_image(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  return rvk_swapchain_image(canvas->swapchain, canvas->swapchainIdx);
}

RvkImage*
rvk_canvas_attach_acquire_color(RvkCanvas* canvas, RvkPass* pass, const u32 i, const RvkSize size) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_color(pass, i);
  return rvk_attach_acquire_color(canvas->attachPool, spec, size);
}

RvkImage* rvk_canvas_attach_acquire_depth(RvkCanvas* canvas, RvkPass* pass, const RvkSize size) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_depth(pass);
  return rvk_attach_acquire_depth(canvas->attachPool, spec, size);
}

void rvk_canvas_attach_release(RvkCanvas* canvas, RvkImage* img) {
  rvk_attach_release(canvas->attachPool, img);
}

void rvk_canvas_copy(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_copy(job, src, dst);
}

void rvk_canvas_blit(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_blit(job, src, dst);
}

void rvk_canvas_end(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];

  // Transition the swapchain-image to the present phase.
  RvkImage* swapchainImage = rvk_swapchain_image(canvas->swapchain, canvas->swapchainIdx);
  rvk_job_transition(job, swapchainImage, RvkImagePhase_Present);

  VkSemaphore attachmentsReady = null;
  if (canvas->flags & RvkCanvasFlags_Submitted) {
    /**
     * Wait for the other job to release the attachments.
     * Reason is we reuse the attachments in both jobs to avoid wasting gpu memory.
     */
    attachmentsReady = canvas->attachmentsReleased[canvas->jobIdx ^ 1];
  }
  const VkSemaphore endSignals[] = {
      canvas->swapchainPresent[canvas->jobIdx],    // Trigger the present.
      canvas->attachmentsReleased[canvas->jobIdx], // Trigger the next job.
  };

  const VkSemaphore swapchainSema = canvas->swapchainAvailable[canvas->jobIdx];
  rvk_job_end(job, attachmentsReady, swapchainSema, endSignals, array_elems(endSignals));

  rvk_swapchain_enqueue_present(
      canvas->swapchain, canvas->swapchainPresent[canvas->jobIdx], canvas->swapchainIdx);

  rvk_attach_pool_flush(canvas->attachPool);

  canvas->swapchainIdx = sentinel_u32;
  canvas->jobIdx ^= 1;
  canvas->flags |= RvkCanvasFlags_Submitted;
  canvas->flags &= ~RvkCanvasFlags_Active;
}

void rvk_canvas_wait_for_prev_present(const RvkCanvas* canvas) {
  const u32 numBehind = 1;
  rvk_swapchain_wait_for_present(canvas->swapchain, numBehind);
}
