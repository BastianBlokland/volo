#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "attach_internal.h"
#include "canvas_internal.h"
#include "device_internal.h"
#include "job_internal.h"
#include "pass_internal.h"
#include "swapchain_internal.h"

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
  RvkSwapchainIdx swapchainIndices[canvas_job_count];
  VkSemaphore     attachmentsReleased[canvas_job_count];
  VkSemaphore     swapchainAvailable[canvas_job_count];
  VkSemaphore     swapchainPresent[canvas_job_count];
  u32             jobIdx;
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
    const RvkPassConfig* passConfig,
    const u32            passCount) {
  diag_assert(passCount <= rvk_canvas_max_passes);

  RvkSwapchain*  swapchain       = rvk_swapchain_create(dev, window);
  const VkFormat swapchainFormat = rvk_swapchain_format(swapchain);
  RvkAttachPool* attachPool      = rvk_attach_pool_create(dev);
  RvkCanvas*     canvas          = alloc_alloc_t(g_allocHeap, RvkCanvas);

  *canvas = (RvkCanvas){
      .dev        = dev,
      .swapchain  = swapchain,
      .attachPool = attachPool,
  };

  for (u32 i = 0; i != canvas_job_count; ++i) {
    canvas->jobs[i]                = rvk_job_create(dev, swapchainFormat, i, passConfig, passCount);
    canvas->attachmentsReleased[i] = rvk_semaphore_create(dev);
    canvas->swapchainAvailable[i]  = rvk_semaphore_create(dev);
    canvas->swapchainPresent[i]    = rvk_semaphore_create(dev);
    canvas->swapchainIndices[i]    = sentinel_u32;
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

  alloc_free_t(g_allocHeap, canvas);
}

RvkRepository* rvk_canvas_repository(RvkCanvas* canvas) { return canvas->dev->repository; }

void rvk_canvas_stats(const RvkCanvas* canvas, RvkCanvasStats* out) {
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  diag_assert(rvk_job_is_done(job));
  rvk_job_stats(job, out);
}

u16 rvk_canvas_attach_count(const RvkCanvas* canvas) {
  return rvk_attach_pool_count(canvas->attachPool);
}

u64 rvk_canvas_attach_memory(const RvkCanvas* canvas) {
  return rvk_attach_pool_memory(canvas->attachPool);
}

void rvk_canvas_swapchain_stats(const RvkCanvas* canvas, RvkSwapchainStats* out) {
  return rvk_swapchain_stats(canvas->swapchain, out);
}

bool rvk_canvas_begin(RvkCanvas* canvas, const RendSettingsComp* settings, const RvkSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkJob*          job          = canvas->jobs[canvas->jobIdx];
  RvkSwapchainIdx* swapchainIdx = &canvas->swapchainIndices[canvas->jobIdx];
  diag_assert(rvk_job_is_done(job));

  trace_begin("rend_present_acquire", TraceColor_White);
  {
    const VkSemaphore availableSema = canvas->swapchainAvailable[canvas->jobIdx];
    *swapchainIdx = rvk_swapchain_acquire(canvas->swapchain, settings, availableSema, size);
  }
  trace_end();

  if (sentinel_check(*swapchainIdx)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  rvk_job_begin(job);
  return true;
}

RvkPass* rvk_canvas_pass(RvkCanvas* canvas, const u32 passIndex) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  return rvk_job_pass(job, passIndex);
}

RvkImage* rvk_canvas_swapchain_image(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  return rvk_swapchain_image(canvas->swapchain, canvas->swapchainIndices[canvas->jobIdx]);
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

RvkImage* rvk_canvas_attach_acquire_copy(RvkCanvas* canvas, RvkImage* src) {
  RvkImage* res = rvk_canvas_attach_acquire_copy_uninit(canvas, src);

  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_img_copy(job, src, res);

  return res;
}

RvkImage* rvk_canvas_attach_acquire_copy_uninit(RvkCanvas* canvas, RvkImage* src) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  const RvkAttachSpec spec = {
      .vkFormat     = src->vkFormat,
      .capabilities = src->caps,
  };
  RvkImage* res;
  if (src->type == RvkImageType_DepthAttachment) {
    res = rvk_attach_acquire_depth(canvas->attachPool, spec, src->size);
  } else {
    res = rvk_attach_acquire_color(canvas->attachPool, spec, src->size);
  }

  return res;
}

void rvk_canvas_attach_release(RvkCanvas* canvas, RvkImage* img) {
  rvk_attach_release(canvas->attachPool, img);
}

void rvk_canvas_img_clear_color(RvkCanvas* canvas, RvkImage* img, const GeoColor color) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_img_clear_color(job, img, color);
}

void rvk_canvas_img_clear_depth(RvkCanvas* canvas, RvkImage* img, const f32 depth) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_img_clear_depth(job, img, depth);
}

void rvk_canvas_img_copy(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_img_copy(job, src, dst);
}

void rvk_canvas_img_blit(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_img_blit(job, src, dst);
}

void rvk_canvas_barrier_full(const RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob* job = canvas->jobs[canvas->jobIdx];
  rvk_job_barrier_full(job);
}

void rvk_canvas_end(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkJob*               job          = canvas->jobs[canvas->jobIdx];
  const RvkSwapchainIdx swapchainIdx = canvas->swapchainIndices[canvas->jobIdx];

  // Transition the swapchain-image to the present phase.
  RvkImage* swapchainImage = rvk_swapchain_image(canvas->swapchain, swapchainIdx);
  rvk_job_img_transition(job, swapchainImage, RvkImagePhase_Present);

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

  trace_begin("rend_submit", TraceColor_White);
  {
    const VkSemaphore swapchainSema = canvas->swapchainAvailable[canvas->jobIdx];
    rvk_job_end(job, attachmentsReady, swapchainSema, endSignals, array_elems(endSignals));
  }
  trace_end();

  trace_begin("rend_present_enqueue", TraceColor_White);
  {
    rvk_swapchain_enqueue_present(
        canvas->swapchain, canvas->swapchainPresent[canvas->jobIdx], swapchainIdx);
  }
  trace_end();

  rvk_attach_pool_flush(canvas->attachPool);

  canvas->jobIdx ^= 1;
  canvas->flags |= RvkCanvasFlags_Submitted;
  canvas->flags &= ~RvkCanvasFlags_Active;
}

bool rvk_canvas_wait_for_prev_present(const RvkCanvas* canvas) {
  if (sentinel_check(canvas->swapchainIndices[canvas->jobIdx])) {
    return false;
  }

  trace_begin("rend_present_wait", TraceColor_White);
  {
    /**
     * Wait for the previous frame to be rendered and presented.
     */
    rvk_job_wait_for_done(canvas->jobs[canvas->jobIdx]);                  // Wait for rendering.
    rvk_swapchain_wait_for_present(canvas->swapchain, 1 /* numBehind */); // Wait for presenting.
  }
  trace_end();

  return true;
}
