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
#include "statrecorder_internal.h"
#include "swapchain_internal.h"

/**
 * Use two frames for double bufferring:
 * - One being recorded on the cpu.
 * - One being rendered on the gpu.
 */
#define canvas_frame_count 2

typedef enum {
  RvkCanvasFlags_Active    = 1 << 0,
  RvkCanvasFlags_Submitted = 1 << 1, // Submitted at least once.
} RvkCanvasFlags;

typedef struct {
  RvkJob*         job;
  VkSemaphore     attachmentsReleased, swapchainAvailable, swapchainPresent;
  RvkSwapchainIdx swapchainIdx;
  RvkPassHandle   passHandles[rvk_canvas_max_passes];
} RvkCanvasFrame;

struct sRvkCanvas {
  RvkDevice*     dev;
  RvkSwapchain*  swapchain;
  RvkAttachPool* attachPool;
  RvkCanvasFrame frames[canvas_frame_count];
  RvkCanvasFlags flags;
  u32            jobIdx;
  u32            passCount;
  RvkPass*       passes[rvk_canvas_max_passes];
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

  RvkSwapchain*  swapchain  = rvk_swapchain_create(dev, window);
  RvkAttachPool* attachPool = rvk_attach_pool_create(dev);
  RvkCanvas*     canvas     = alloc_alloc_t(g_allocHeap, RvkCanvas);

  *canvas = (RvkCanvas){
      .dev        = dev,
      .swapchain  = swapchain,
      .attachPool = attachPool,
      .passCount  = passCount,
  };

  for (u32 i = 0; i != canvas_frame_count; ++i) {
    canvas->frames[i] = (RvkCanvasFrame){
        .job                 = rvk_job_create(dev, i),
        .attachmentsReleased = rvk_semaphore_create(dev),
        .swapchainAvailable  = rvk_semaphore_create(dev),
        .swapchainPresent    = rvk_semaphore_create(dev),
        .swapchainIdx        = sentinel_u32,
    };
    mem_set(array_mem(canvas->frames[i].passHandles), 0xFF);
  }

  for (u32 passIdx = 0; passIdx != passCount; ++passIdx) {
    canvas->passes[passIdx] = rvk_pass_create(dev, &passConfig[passIdx]);
  }

  log_d(
      "Vulkan canvas created",
      log_param("size", gap_vector_fmt(gap_window_param(window, GapParam_WindowSize))));

  return canvas;
}

void rvk_canvas_destroy(RvkCanvas* canvas) {
  rvk_device_wait_idle(canvas->dev);

  array_for_t(canvas->frames, RvkCanvasFrame, frame) {
    rvk_job_destroy(frame->job);
    vkDestroySemaphore(canvas->dev->vkDev, frame->attachmentsReleased, &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, frame->swapchainAvailable, &canvas->dev->vkAlloc);
    vkDestroySemaphore(canvas->dev->vkDev, frame->swapchainPresent, &canvas->dev->vkAlloc);
  }

  for (u32 passIdx = 0; passIdx != canvas->passCount; ++passIdx) {
    rvk_pass_destroy(canvas->passes[passIdx]);
  }

  rvk_swapchain_destroy(canvas->swapchain);
  rvk_attach_pool_destroy(canvas->attachPool);

  log_d("Vulkan canvas destroyed");

  alloc_free_t(g_allocHeap, canvas);
}

RvkRepository* rvk_canvas_repository(RvkCanvas* canvas) { return canvas->dev->repository; }

void rvk_canvas_stats(const RvkCanvas* canvas, RvkCanvasStats* out) {
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert(rvk_job_is_done(frame->job));

  RvkJobStats jobStats;
  rvk_job_stats(frame->job, &jobStats);

  out->waitForGpuDur = jobStats.waitForGpuDur;
  out->gpuExecDur    = jobStats.gpuExecDur;

  out->passCount = 0;
  for (u32 passIdx = 0; passIdx != canvas->passCount; ++passIdx) {
    const RvkPass*      pass      = canvas->passes[passIdx];
    const RvkPassHandle passFrame = frame->passHandles[passIdx];
    if (sentinel_check(passFrame)) {
      continue;
    }

    const RvkSize sizeMax         = rvk_pass_stat_size_max(pass, passFrame);
    out->passes[out->passCount++] = (RendStatsPass){
        .name        = rvk_pass_config(pass)->name, // Persistently allocated.
        .gpuExecDur  = rvk_pass_stat_duration(pass, passFrame),
        .sizeMax[0]  = sizeMax.width,
        .sizeMax[1]  = sizeMax.height,
        .invocations = rvk_pass_stat_invocations(pass, passFrame),
        .draws       = rvk_pass_stat_draws(pass, passFrame),
        .instances   = rvk_pass_stat_instances(pass, passFrame),
        .vertices    = rvk_pass_stat_pipeline(pass, passFrame, RvkStat_InputAssemblyVertices),
        .primitives  = rvk_pass_stat_pipeline(pass, passFrame, RvkStat_InputAssemblyPrimitives),
        .shadersVert = rvk_pass_stat_pipeline(pass, passFrame, RvkStat_ShaderInvocationsVert),
        .shadersFrag = rvk_pass_stat_pipeline(pass, passFrame, RvkStat_ShaderInvocationsFrag),
    };
  }
}

u16 rvk_canvas_attach_count(const RvkCanvas* canvas) {
  return rvk_attach_pool_count(canvas->attachPool);
}

u64 rvk_canvas_attach_memory(const RvkCanvas* canvas) {
  return rvk_attach_pool_memory(canvas->attachPool);
}

void rvk_canvas_swapchain_stats(const RvkCanvas* canvas, RvkSwapchainStats* out) {
  rvk_swapchain_stats(canvas->swapchain, out);
}

bool rvk_canvas_begin(RvkCanvas* canvas, const RendSettingsComp* settings, const RvkSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert(rvk_job_is_done(frame->job));

  trace_begin("rend_present_acquire", TraceColor_White);
  {
    const VkSemaphore availableSema = frame->swapchainAvailable;
    frame->swapchainIdx = rvk_swapchain_acquire(canvas->swapchain, settings, availableSema, size);
  }
  trace_end();

  if (sentinel_check(frame->swapchainIdx)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  rvk_job_begin(frame->job);

  for (u32 passIdx = 0; passIdx != canvas->passCount; ++passIdx) {
    RvkPass* pass = canvas->passes[passIdx];
    if (!sentinel_check(frame->passHandles[passIdx])) {
      rvk_pass_frame_release(pass, frame->passHandles[passIdx]);
    }
    frame->passHandles[passIdx] = rvk_pass_frame_begin(pass, frame->job);
  }

  return true;
}

u32 rvk_canvas_pass_count(const RvkCanvas* canvas) { return canvas->passCount; }

RvkPass* rvk_canvas_pass(RvkCanvas* canvas, const u32 passIndex) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  diag_assert(passIndex < canvas->passCount);

  return canvas->passes[passIndex];
}

RvkImage* rvk_canvas_swapchain_image(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  return rvk_swapchain_image(canvas->swapchain, frame->swapchainIdx);
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

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_img_copy(frame->job, src, res);

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

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_img_clear_color(frame->job, img, color);
}

void rvk_canvas_img_clear_depth(RvkCanvas* canvas, RvkImage* img, const f32 depth) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_img_clear_depth(frame->job, img, depth);
}

void rvk_canvas_img_copy(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_img_copy(frame->job, src, dst);
}

void rvk_canvas_img_blit(RvkCanvas* canvas, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_img_blit(frame->job, src, dst);
}

void rvk_canvas_barrier_full(const RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  rvk_job_barrier_full(frame->job);
}

void rvk_canvas_end(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];

  for (u32 passIdx = 0; passIdx != canvas->passCount; ++passIdx) {
    rvk_pass_frame_end(canvas->passes[passIdx], frame->passHandles[passIdx]);
  }

  // Transition the swapchain-image to the present phase.
  RvkImage* swapchainImage = rvk_swapchain_image(canvas->swapchain, frame->swapchainIdx);
  rvk_job_img_transition(frame->job, swapchainImage, RvkImagePhase_Present);

  VkSemaphore attachmentsReady = null;
  if (canvas->flags & RvkCanvasFlags_Submitted) {
    /**
     * Wait for the other job to release the attachments.
     * Reason is we reuse the attachments in both jobs to avoid wasting gpu memory.
     */
    attachmentsReady = canvas->frames[canvas->jobIdx ^ 1].attachmentsReleased;
  }
  const VkSemaphore endSignals[] = {
      frame->swapchainPresent,    // Trigger the present.
      frame->attachmentsReleased, // Trigger the next job.
  };

  trace_begin("rend_submit", TraceColor_White);
  {
    const VkSemaphore swapchainSema = frame->swapchainAvailable;
    rvk_job_end(frame->job, attachmentsReady, swapchainSema, endSignals, array_elems(endSignals));
  }
  trace_end();

  trace_begin("rend_present_enqueue", TraceColor_White);
  {
    rvk_swapchain_enqueue_present(canvas->swapchain, frame->swapchainPresent, frame->swapchainIdx);
  }
  trace_end();

  rvk_attach_pool_flush(canvas->attachPool);

  canvas->jobIdx ^= 1;
  canvas->flags |= RvkCanvasFlags_Submitted;
  canvas->flags &= ~RvkCanvasFlags_Active;
}

bool rvk_canvas_wait_for_prev_present(const RvkCanvas* canvas) {
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  if (sentinel_check(frame->swapchainIdx)) {
    return false;
  }

  trace_begin("rend_present_wait", TraceColor_White);
  {
    /**
     * Wait for the previous frame to be rendered and presented.
     */
    rvk_job_wait_for_done(frame->job);                                    // Wait for rendering.
    rvk_swapchain_wait_for_present(canvas->swapchain, 1 /* numBehind */); // Wait for presenting.
  }
  trace_end();

  return true;
}
