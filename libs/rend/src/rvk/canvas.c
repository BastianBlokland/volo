#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/math.h"
#include "log/logger.h"
#include "trace/tracer.h"

#include "attach.h"
#include "canvas.h"
#include "device.h"
#include "job.h"
#include "lib.h"
#include "pass.h"
#include "statrecorder.h"
#include "swapchain.h"

/**
 * Use two frames for double buffering:
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
  u64             frameIdx;
  VkSemaphore     outputAvailable;
  RvkSwapchainIdx swapchainIdx;      // sentinel_u32 when not acquired yet or failed to acquire.
  RvkImage*       swapchainFallback; // Only used when the preferred format is not available.
  RvkPass*        passes[rvk_canvas_max_passes];
  RvkPassHandle   passFrames[rvk_canvas_max_passes];
} RvkCanvasFrame;

struct sRvkCanvas {
  RvkDevice*     dev;
  RvkSwapchain*  swapchain;
  RvkAttachPool* attachPool;
  RvkCanvasFrame frames[canvas_frame_count];
  RvkCanvasFlags flags;
  u32            jobIdx;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call_checked(dev, createSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_semaphore_destroy(RvkDevice* dev, const VkSemaphore sema) {
  rvk_call(dev, destroySemaphore, dev->vkDev, sema, &dev->vkAlloc);
}

RvkCanvas* rvk_canvas_create(RvkLib* lib, RvkDevice* dev, const GapWindowComp* window) {
  RvkSwapchain*  swapchain  = rvk_swapchain_create(lib, dev, window);
  RvkAttachPool* attachPool = rvk_attach_pool_create(dev);
  RvkCanvas*     canvas     = alloc_alloc_t(g_allocHeap, RvkCanvas);

  *canvas = (RvkCanvas){.dev = dev, .swapchain = swapchain, .attachPool = attachPool};

  for (u32 i = 0; i != canvas_frame_count; ++i) {
    RvkCanvasFrame* frame = &canvas->frames[i];

    *frame = (RvkCanvasFrame){
        .job             = rvk_job_create(dev, i),
        .outputAvailable = rvk_semaphore_create(dev),
        .swapchainIdx    = sentinel_u32,
    };
    mem_set(array_mem(frame->passFrames), 0xFF);

    rvk_debug_name_semaphore(dev, frame->outputAvailable, "canvas_output_{}", fmt_int(i));
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
    rvk_semaphore_destroy(canvas->dev, frame->outputAvailable);
  }

  rvk_swapchain_destroy(canvas->swapchain);
  rvk_attach_pool_destroy(canvas->attachPool);

  log_d("Vulkan canvas destroyed");

  alloc_free_t(g_allocHeap, canvas);
}

const RvkRepository* rvk_canvas_repository(const RvkCanvas* canvas) {
  return canvas->dev->repository;
}

RvkAttachPool* rvk_canvas_attach_pool(RvkCanvas* canvas) { return canvas->attachPool; }

RvkJob* rvk_canvas_job(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  return canvas->frames[canvas->jobIdx].job;
}

void rvk_canvas_stats(const RvkCanvas* canvas, RvkCanvasStats* out) {
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert(rvk_job_is_done(frame->job));

  if (!(canvas->flags & RvkCanvasFlags_Submitted)) {
    *out = (RvkCanvasStats){0};
    return;
  }

  RvkJobStats jobStats;
  rvk_job_stats(frame->job, &jobStats);

  out->waitForGpuDur = jobStats.cpuWaitDur;
  out->gpuWaitDur    = time_steady_duration(jobStats.gpuWaitBegin, jobStats.gpuWaitEnd);
  out->gpuExecDur    = time_steady_duration(jobStats.gpuTimeBegin, jobStats.gpuTimeEnd);

  // NOTE: Consider the wait-time as non-executing.
  out->gpuExecDur = math_max(out->gpuExecDur - out->gpuWaitDur, 0);

  const u32 copyStatsCount = math_min(jobStats.copyCount, rvk_job_copy_stats_max);
  out->gpuCopyDur          = 0;
  for (u32 copyIdx = 0; copyIdx != copyStatsCount; ++copyIdx) {
    const TimeDuration copyBegin = jobStats.copyStats[copyIdx].gpuTimeBegin;
    const TimeDuration copyEnd   = jobStats.copyStats[copyIdx].gpuTimeEnd;
    out->gpuCopyDur += time_steady_duration(copyBegin, copyEnd);
  }

  out->passCount = 0;
  for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
    const RvkPass* pass = frame->passes[passIdx];
    if (!pass) {
      break; // End of the used passes.
    }
    const RvkPassHandle passFrame = frame->passFrames[passIdx];
    diag_assert(!sentinel_check(passFrame));

    RvkPassStats passStats;
    rvk_pass_stats(pass, passFrame, &passStats);

    out->passes[out->passCount++] = (RendStatsPass){
        .name        = rvk_pass_config(pass)->name, // Persistently allocated.
        .gpuExecDur  = passStats.duration,
        .sizeMax[0]  = passStats.sizeMax.width,
        .sizeMax[1]  = passStats.sizeMax.height,
        .invocations = passStats.invocationCount,
        .draws       = passStats.drawCount,
        .instances   = passStats.instanceCount,
        .vertices    = rvk_pass_stats_pipeline(pass, passFrame, RvkStat_InputAssemblyVertices),
        .primitives  = rvk_pass_stats_pipeline(pass, passFrame, RvkStat_InputAssemblyPrimitives),
        .shadersVert = rvk_pass_stats_pipeline(pass, passFrame, RvkStat_ShaderInvocationsVert),
        .shadersFrag = rvk_pass_stats_pipeline(pass, passFrame, RvkStat_ShaderInvocationsFrag),
    };
  }
}

#ifdef VOLO_TRACE
static void rvk_canvas_push_traces_gpu(const RvkCanvasFrame* frame, const RvkJobStats* jobStats) {
  trace_custom_begin_msg(
      "gpu",
      "frame",
      TraceColor_Blue,
      "frame-{} [{}]",
      fmt_int(frame->frameIdx),
      fmt_int(frame->swapchainIdx));
  {
    for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
      const RvkPass* pass = frame->passes[passIdx];
      if (!pass) {
        break; // End of the used passes.
      }
      const RvkPassHandle passFrame = frame->passFrames[passIdx];
      diag_assert(!sentinel_check(passFrame));

      const String passName = rvk_pass_config(pass)->name;

      RvkPassStats passStats;
      rvk_pass_stats(pass, passFrame, &passStats);

      for (u16 invocIdx = 0; invocIdx != passStats.invocationCount; ++invocIdx) {
        RvkPassStatsInvoc stats;
        rvk_pass_stats_invoc(pass, passFrame, invocIdx, &stats);

        trace_custom_begin_msg("gpu", "pass", TraceColor_Green, "pass-{}", fmt_text(passName));
        const TimeDuration dur = time_steady_duration(stats.gpuTimeBegin, stats.gpuTimeEnd);
        trace_custom_end("gpu", stats.gpuTimeBegin, dur);
      }
    }

    const u32 copyStatsCount = math_min(jobStats->copyCount, rvk_job_copy_stats_max);
    for (u32 copyIdx = 0; copyIdx != copyStatsCount; ++copyIdx) {
      trace_custom_begin("gpu", "copy", TraceColor_Red);
      const TimeDuration copyBegin = jobStats->copyStats[copyIdx].gpuTimeBegin;
      const TimeDuration copyEnd   = jobStats->copyStats[copyIdx].gpuTimeEnd;
      const TimeDuration copyDur   = time_steady_duration(copyBegin, copyEnd);
      trace_custom_end("gpu", copyBegin, copyDur);
    }

    trace_custom_begin("gpu", "wait", TraceColor_White);
    const TimeDuration waitDur = time_steady_duration(jobStats->gpuWaitBegin, jobStats->gpuWaitEnd);
    trace_custom_end("gpu", jobStats->gpuWaitBegin, waitDur);
  }
  const TimeDuration jobDur = time_steady_duration(jobStats->gpuTimeBegin, jobStats->gpuTimeEnd);
  trace_custom_end("gpu", jobStats->gpuTimeBegin, jobDur);
}

static void rvk_canvas_push_traces_display(const RvkSwapchain* swapchain) {
  const RvkSwapchainPresentHistory history = rvk_swapchain_past_presents(swapchain);

  for (u32 i = 0; i != history.count; ++i) {
    trace_custom_begin_msg(
        "display", "frame", TraceColor_Blue, "frame-{}", fmt_int(history.data[i].frameIdx));
    trace_custom_end("display", history.data[i].dequeueTime, history.data[i].duration);
  }
}

void rvk_canvas_push_traces(const RvkCanvas* canvas) {
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert(rvk_job_is_done(frame->job));

  if (!(canvas->flags & RvkCanvasFlags_Submitted)) {
    return;
  }
  if (!rvk_job_calibrated_timestamps(frame->job)) {
    return; // GPU traces require calibrated timestamps.
  }

  RvkJobStats jobStats;
  rvk_job_stats(frame->job, &jobStats);

  rvk_canvas_push_traces_gpu(frame, &jobStats);
  rvk_canvas_push_traces_display(canvas->swapchain);
}
#endif // VOLO_TRACE

bool rvk_canvas_begin(
    RvkCanvas* canvas, const RendSettingsComp* settings, const u64 frameIdx, const RvkSize size) {
  diag_assert_msg(!(canvas->flags & RvkCanvasFlags_Active), "Canvas already active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert(rvk_job_is_done(frame->job));
  diag_assert(frameIdx == 0 || frameIdx > frame->frameIdx);

  frame->frameIdx     = frameIdx;
  frame->swapchainIdx = sentinel_u32;

  if (!rvk_swapchain_prepare(canvas->swapchain, settings, size)) {
    return false;
  }

  canvas->flags |= RvkCanvasFlags_Active;
  rvk_job_begin(frame->job, RvkJobPhase_First);

  // Cleanup the last frame's passes.
  for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
    if (!frame->passes[passIdx]) {
      break; // End of the used passes.
    }
    diag_assert(!sentinel_check(frame->passFrames[passIdx]));
    rvk_pass_frame_release(frame->passes[passIdx], frame->passFrames[passIdx]);
    frame->passes[passIdx] = null;
  }

  return true;
}

void rvk_canvas_pass_push(RvkCanvas* canvas, RvkPass* pass) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];

  // Check if this pass was already pushed this frame.
  for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
    if (!frame->passes[passIdx]) {
      break; // End of the used passes.
    }
    if (frame->passes[passIdx] == pass) {
      return; // Already present in this frame.
    }
  }

  // Register the pass to this frame.
  for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
    if (!frame->passes[passIdx]) {
      frame->passes[passIdx]     = pass;
      frame->passFrames[passIdx] = rvk_pass_frame_begin(pass, frame->job);
      return;
    }
  }

  diag_crash_msg("Canvas pass limit exceeded");
}

RvkJobPhase rvk_canvas_phase(const RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  return rvk_job_phase(frame->job);
}

void rvk_canvas_phase_output(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  if (rvk_job_phase(frame->job) == RvkJobPhase_Output) {
    return;
  }
  trace_begin("rend_submit", TraceColor_Blue);
  rvk_job_advance(frame->job); // Submit the previous phase.
  trace_end();

  trace_begin("rend_swapchain_acquire", TraceColor_Blue);
  frame->swapchainIdx = rvk_swapchain_acquire(canvas->swapchain, frame->outputAvailable);
  trace_end();
}

void rvk_canvas_swapchain_stats(const RvkCanvas* canvas, RvkSwapchainStats* out) {
  rvk_swapchain_stats(canvas->swapchain, out);
}

RvkSize rvk_canvas_swapchain_size(const RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  return rvk_swapchain_size(canvas->swapchain);
}

bool rvk_canvas_swapchain_can_throttle(const RvkCanvas* canvas) {
  return rvk_swapchain_can_throttle(canvas->swapchain);
}

RvkImage* rvk_canvas_swapchain_image(RvkCanvas* canvas) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");

  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];
  diag_assert_msg(
      rvk_job_phase(frame->job) == RvkJobPhase_Output,
      "Swapchain image can only be acquired in the output phase");

  if (sentinel_check(frame->swapchainIdx)) {
    return null; // Failed to acquire a swapchain image.
  }

  if (rvk_swapchain_format(canvas->swapchain) == canvas->dev->preferredSwapchainFormat) {
    return rvk_swapchain_image(canvas->swapchain, frame->swapchainIdx);
  }

  if (frame->swapchainFallback) {
    return frame->swapchainFallback;
  }
  const RvkSize       size = rvk_swapchain_size(canvas->swapchain);
  const RvkAttachSpec spec = {
      .vkFormat     = canvas->dev->preferredSwapchainFormat,
      .capabilities = RvkImageCapability_AttachmentColor | RvkImageCapability_TransferSource,
  };
  return frame->swapchainFallback = rvk_attach_acquire_color(canvas->attachPool, spec, size);
}

void rvk_canvas_end(RvkCanvas* canvas, const u16 presentFrequency) {
  diag_assert_msg(canvas->flags & RvkCanvasFlags_Active, "Canvas not active");
  RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];

  for (u32 passIdx = 0; passIdx != rvk_canvas_max_passes; ++passIdx) {
    if (!frame->passes[passIdx]) {
      break; // End of the used passes.
    }
    rvk_pass_frame_end(frame->passes[passIdx], frame->passFrames[passIdx]);
  }

  const bool hasSwapchain = !sentinel_check(frame->swapchainIdx);
  if (hasSwapchain) {
    RvkImage* swapchainImage = rvk_swapchain_image(canvas->swapchain, frame->swapchainIdx);
    // If using a swapchain-fallback copy the final content into the swapchain.
    if (frame->swapchainFallback) {
      rvk_job_img_blit(frame->job, frame->swapchainFallback, swapchainImage);
      rvk_attach_release(canvas->attachPool, frame->swapchainFallback);
      frame->swapchainFallback = null;
    }
    // Transition the swapchain-image to the present phase.
    rvk_job_img_transition(frame->job, swapchainImage, RvkImagePhase_Present);
  }

  trace_begin("rend_submit", TraceColor_Blue);
  if (hasSwapchain) {
    const VkSemaphore waitSignal   = frame->outputAvailable;
    const VkSemaphore endSignals[] = {
        rvk_swapchain_semaphore(canvas->swapchain, frame->swapchainIdx), // Trigger present.
    };
    rvk_job_end(frame->job, waitSignal, endSignals, array_elems(endSignals));
  } else {
    rvk_job_end(frame->job, null, null, 0);
  }
  trace_end();

  if (hasSwapchain) {
    trace_begin("rend_present_enqueue", TraceColor_Blue);
    rvk_swapchain_enqueue_present(
        canvas->swapchain, frame->swapchainIdx, frame->frameIdx, presentFrequency);
    trace_end();
  }

  rvk_attach_pool_flush(canvas->attachPool);

  canvas->jobIdx ^= 1;
  canvas->flags |= RvkCanvasFlags_Submitted;
  canvas->flags &= ~RvkCanvasFlags_Active;
}

bool rvk_canvas_wait_for_prev_present(const RvkCanvas* canvas) {
  const RvkCanvasFrame* frame = &canvas->frames[canvas->jobIdx];

  /**
   * Wait for the previous frame to be rendered and presented.
   */

  trace_begin_msg("rend_wait_job", TraceColor_White, "rend_wait_{}", fmt_int(frame->frameIdx));
  rvk_job_wait_for_done(frame->job);
  trace_end();

  if (sentinel_check(frame->swapchainIdx)) {
    return false;
  }

  trace_begin("rend_wait_swapchain", TraceColor_White);
  rvk_swapchain_wait_for_present(canvas->swapchain, 1 /* numBehind */);
  trace_end();

  return true;
}
