#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"

#include "canvas_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "renderer_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "uniform_internal.h"

typedef RvkPass* RvkPassPtr;

typedef enum {
  RvkRenderer_Active = 1 << 0,
} RvkRendererFlags;

struct sRvkRenderer {
  RvkDevice*       dev;
  u32              rendererId;
  RvkUniformPool*  uniformPool;
  RvkStopwatch*    stopwatch;
  RvkPass*         passes[RvkCanvasPass_Count];
  VkFence          fenceRenderDone;
  VkCommandPool    vkCmdPool;
  VkCommandBuffer  vkDrawBuffer;
  RvkRendererFlags flags;

  RvkStopwatchRecord timeRecBegin, timeRecEnd;
  TimeDuration       waitForRenderDur;
};

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  const VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call(vkCreateFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandPool rvk_commandpool_create(RvkDevice* dev, const u32 queueIndex) {
  VkCommandPoolCreateInfo createInfo = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queueIndex,
      .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
  };
  VkCommandPool result;
  rvk_call(vkCreateCommandPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandBuffer rvk_commandbuffer_create(RvkDevice* dev, VkCommandPool vkCmdPool) {
  const VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = vkCmdPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer result;
  rvk_call(vkAllocateCommandBuffers, dev->vkDev, &allocInfo, &result);
  return result;
}

static void rvk_commandpool_reset(RvkDevice* dev, VkCommandPool vkCmdPool) {
  rvk_call(vkResetCommandPool, dev->vkDev, vkCmdPool, 0);
}

static void rvk_commandbuffer_begin(VkCommandBuffer vkCmdBuf) {
  const VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call(vkBeginCommandBuffer, vkCmdBuf, &beginInfo);
}

static void rvk_commandbuffer_end(VkCommandBuffer vkCmdBuf) {
  rvk_call(vkEndCommandBuffer, vkCmdBuf);
}

static void rvk_renderer_submit(
    RvkRenderer*       rend,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount) {

  VkSemaphore          waitSemaphores[2];
  VkPipelineStageFlags waitStages[2];
  u32                  waitCount = 0;

  if (waitForDeps) {
    waitSemaphores[waitCount] = waitForDeps;
    waitStages[waitCount]     = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    ++waitCount;
  }
  if (waitForTarget) {
    /**
     * At the moment we do a single submit for the whole frame and thus we need to wait with all
     * output until the target image is available. Potentially this could be improved by splitting
     * the rendering into multiple submits.
     */
    waitSemaphores[waitCount] = waitForTarget;
    waitStages[waitCount] =
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    ++waitCount;
  }

  const VkCommandBuffer commandBuffers[] = {rend->vkDrawBuffer};

  const VkSubmitInfo submitInfos[] = {
      {
          .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount   = waitCount,
          .pWaitSemaphores      = waitSemaphores,
          .pWaitDstStageMask    = waitStages,
          .commandBufferCount   = array_elems(commandBuffers),
          .pCommandBuffers      = commandBuffers,
          .signalSemaphoreCount = signalCount,
          .pSignalSemaphores    = signals,
      },
  };
  thread_mutex_lock(rend->dev->queueSubmitMutex);
  rvk_call(
      vkQueueSubmit,
      rend->dev->vkGraphicsQueue,
      array_elems(submitInfos),
      submitInfos,
      rend->fenceRenderDone);
  thread_mutex_unlock(rend->dev->queueSubmitMutex);
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev, const u32 rendererId) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);

  RvkUniformPool* uniformPool = rvk_uniform_pool_create(dev);
  RvkStopwatch*   stopwatch   = rvk_stopwatch_create(dev);

  VkCommandPool vkCmdPool = rvk_commandpool_create(dev, dev->graphicsQueueIndex);
  rvk_debug_name_cmdpool(dev->debug, vkCmdPool, "renderer_{}", fmt_int(rendererId));

  VkCommandBuffer vkDrawBuffer = rvk_commandbuffer_create(dev, vkCmdPool);

  *renderer = (RvkRenderer){
      .dev             = dev,
      .uniformPool     = uniformPool,
      .stopwatch       = stopwatch,
      .rendererId      = rendererId,
      .fenceRenderDone = rvk_fence_create(dev, true),
      .vkCmdPool       = vkCmdPool,
      .vkDrawBuffer    = vkDrawBuffer,
  };

  // clang-format off
  {
    const RvkPassFlags flags = RvkPassFlags_Clear |
      RvkPassFlags_Color1 | RvkPassFlags_Color1Srgb |      // Attachment color1 (srgb)  : color (rgb) and roughness (a).
      RvkPassFlags_Color2 |                                // Attachment color2 (linear): normal (rgb) and tags (a).
      RvkPassFlags_Depth | RvkPassFlags_DepthStore;        // Attachment depth.
    renderer->passes[RvkCanvasPass_Geometry] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, rvk_canvas_pass_name(RvkCanvasPass_Geometry));
  }
  {
    const RvkPassFlags flags = RvkPassFlags_ClearColor |
      RvkPassFlags_Color1 | RvkPassFlags_Color1Srgb    |   // Attachment color1 (srgb): color (rgb).
      RvkPassFlags_Depth | RvkPassFlags_DepthLoadTransfer; // Attachment depth.
    renderer->passes[RvkCanvasPass_Forward] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, rvk_canvas_pass_name(RvkCanvasPass_Forward));
  }
  {
    const RvkPassFlags flags = RvkPassFlags_ClearDepth |
      RvkPassFlags_Depth | RvkPassFlags_DepthStore;        // Attachment depth.
    renderer->passes[RvkCanvasPass_Shadow] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, rvk_canvas_pass_name(RvkCanvasPass_Shadow));
  }
  {
    const RvkPassFlags flags =
      RvkPassFlags_Color1 | RvkPassFlags_Color1Single;     // Attachment color1 (linear): occlusion (r).
    renderer->passes[RvkCanvasPass_AmbientOcclusion] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, rvk_canvas_pass_name(RvkCanvasPass_AmbientOcclusion));
  }
  // clang-format on

  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  rvk_renderer_wait_for_done(rend);

  array_for_t(rend->passes, RvkPassPtr, itr) { rvk_pass_destroy(*itr); }

  rvk_uniform_pool_destroy(rend->uniformPool);
  rvk_stopwatch_destroy(rend->stopwatch);

  vkDestroyCommandPool(rend->dev->vkDev, rend->vkCmdPool, &rend->dev->vkAlloc);
  vkDestroyFence(rend->dev->vkDev, rend->fenceRenderDone, &rend->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  const TimeSteady waitStart = time_steady_clock();

  rvk_call(vkWaitForFences, rend->dev->vkDev, 1, &rend->fenceRenderDone, true, u64_max);

  ((RvkRenderer*)rend)->waitForRenderDur += time_steady_duration(waitStart, time_steady_clock());
}

RvkCanvasStats rvk_renderer_stats(const RvkRenderer* rend) {
  rvk_renderer_wait_for_done(rend);

  const u64 timestampBegin = rvk_stopwatch_query(rend->stopwatch, rend->timeRecBegin);
  const u64 timestampEnd   = rvk_stopwatch_query(rend->stopwatch, rend->timeRecEnd);

  RvkCanvasStats result;
  result.renderDur        = time_nanoseconds(timestampEnd - timestampBegin);
  result.waitForRenderDur = rend->waitForRenderDur;

  for (RvkCanvasPass passIdx = 0; passIdx != RvkCanvasPass_Count; ++passIdx) {
    const RvkPass* pass = rend->passes[passIdx];
    if (rvk_pass_recorded(pass)) {
      result.passes[passIdx] = (RendStatPass){
          .dur         = rvk_pass_duration(pass),
          .size[0]     = rvk_pass_size(pass).width,
          .size[1]     = rvk_pass_size(pass).height,
          .draws       = (u16)rvk_pass_stat(pass, RvkStat_Draws),
          .instances   = (u32)rvk_pass_stat(pass, RvkStat_Instances),
          .vertices    = rvk_pass_stat(pass, RvkStat_InputAssemblyVertices),
          .primitives  = rvk_pass_stat(pass, RvkStat_InputAssemblyPrimitives),
          .shadersVert = rvk_pass_stat(pass, RvkStat_ShaderInvocationsVert),
          .shadersFrag = rvk_pass_stat(pass, RvkStat_ShaderInvocationsFrag),
      };
    } else {
      // Pass has not been recorded; no stats available.
      result.passes[passIdx] = (RendStatPass){0};
    }
  }

  return result;
}

void rvk_renderer_begin(RvkRenderer* rend) {
  diag_assert_msg(!(rend->flags & RvkRenderer_Active), "Renderer already active");

  rend->flags |= RvkRenderer_Active;
  rend->waitForRenderDur = 0;

  rvk_renderer_wait_for_done(rend);
  rvk_uniform_reset(rend->uniformPool);
  rvk_commandpool_reset(rend->dev, rend->vkCmdPool);

  rvk_commandbuffer_begin(rend->vkDrawBuffer);
  rvk_stopwatch_reset(rend->stopwatch, rend->vkDrawBuffer);

  array_for_t(rend->passes, RvkPassPtr, itr) { rvk_pass_reset(*itr); }

  rend->timeRecBegin = rvk_stopwatch_mark(rend->stopwatch, rend->vkDrawBuffer);
  rvk_debug_label_begin(
      rend->dev->debug,
      rend->vkDrawBuffer,
      geo_color_teal,
      "renderer_{}",
      fmt_int(rend->rendererId));
}

RvkPass* rvk_renderer_pass(RvkRenderer* rend, const RvkCanvasPass pass) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");
  diag_assert(pass < RvkCanvasPass_Count);
  return rend->passes[pass];
}

void rvk_renderer_copy(RvkRenderer* rend, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");

  rvk_debug_label_begin(rend->dev->debug, rend->vkDrawBuffer, geo_color_purple, "copy");

  rvk_image_transition(src, rend->vkDrawBuffer, RvkImagePhase_TransferSource);
  rvk_image_transition(dst, rend->vkDrawBuffer, RvkImagePhase_TransferDest);

  rvk_image_copy(src, dst, rend->vkDrawBuffer);

  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
}

void rvk_renderer_blit(RvkRenderer* rend, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");

  rvk_debug_label_begin(rend->dev->debug, rend->vkDrawBuffer, geo_color_purple, "blit");

  rvk_image_transition(src, rend->vkDrawBuffer, RvkImagePhase_TransferSource);
  rvk_image_transition(dst, rend->vkDrawBuffer, RvkImagePhase_TransferDest);

  rvk_image_blit(src, dst, rend->vkDrawBuffer);

  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
}

void rvk_renderer_transition(RvkRenderer* rend, RvkImage* img, const RvkImagePhase targetPhase) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");

  rvk_image_transition(img, rend->vkDrawBuffer, targetPhase);
}

void rvk_renderer_end(
    RvkRenderer*       rend,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");

  array_for_t(rend->passes, RvkPassPtr, itr) {
    diag_assert_msg(
        !rvk_pass_active(*itr), "Pass '{}' is still active", fmt_text(rvk_pass_name(*itr)));
  }

  rend->timeRecEnd = rvk_stopwatch_mark(rend->stopwatch, rend->vkDrawBuffer);
  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->fenceRenderDone);
  rvk_renderer_submit(rend, waitForDeps, waitForTarget, signals, signalCount);

  rend->flags &= ~RvkRenderer_Active;
}
