#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"

#include "device_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "renderer_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "types_internal.h"
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
  RvkPass*         passes[RvkRenderPass_Count];
  VkSemaphore      semaphoreBegin, semaphoreDone;
  VkFence          fenceRenderDone;
  VkCommandPool    vkCmdPool;
  VkCommandBuffer  vkDrawBuffer;
  RvkRendererFlags flags;

  RvkImage*          currentTarget;
  RvkImagePhase      currentTargetPhase;
  RvkSize            currentResolution;
  RvkStopwatchRecord timeRecBegin, timeRecEnd;
  TimeDuration       waitForRenderDur;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

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

static void rvk_renderer_submit(RvkRenderer* rend) {

  const VkCommandBuffer      commandBuffers[]   = {rend->vkDrawBuffer};
  const VkSemaphore          waitSemaphores[]   = {rend->semaphoreBegin};
  const VkPipelineStageFlags waitStages[]       = {VK_PIPELINE_STAGE_TRANSFER_BIT};
  const VkSemaphore          signalSemaphores[] = {rend->semaphoreDone};

  const VkSubmitInfo submitInfos[] = {
      {
          .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount   = array_elems(waitSemaphores),
          .pWaitSemaphores      = waitSemaphores,
          .pWaitDstStageMask    = waitStages,
          .commandBufferCount   = array_elems(commandBuffers),
          .pCommandBuffers      = commandBuffers,
          .signalSemaphoreCount = array_elems(signalSemaphores),
          .pSignalSemaphores    = signalSemaphores,
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

static void rvk_renderer_blit_to_output(RvkRenderer* rend, RvkPass* pass) {
  rvk_debug_label_begin(rend->dev->debug, rend->vkDrawBuffer, geo_color_purple, "blit_to_output");

  RvkImage* src  = rvk_pass_output(pass, RvkPassOutput_Color1);
  RvkImage* dest = rend->currentTarget;

  rvk_image_transition(src, rend->vkDrawBuffer, RvkImagePhase_TransferSource);
  rvk_image_transition(dest, rend->vkDrawBuffer, RvkImagePhase_TransferDest);

  rvk_image_blit(src, dest, rend->vkDrawBuffer);

  rvk_image_transition(dest, rend->vkDrawBuffer, rend->currentTargetPhase);

  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
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
      .semaphoreBegin  = rvk_semaphore_create(dev),
      .semaphoreDone   = rvk_semaphore_create(dev),
      .fenceRenderDone = rvk_fence_create(dev, true),
      .vkCmdPool       = vkCmdPool,
      .vkDrawBuffer    = vkDrawBuffer,
  };

  // clang-format off
  {
    const RvkPassFlags flags = RvkPassFlags_Clear |
      RvkPassFlags_Color1 | RvkPassFlags_SrgbColor1 | // Attachment color1 (srgb)  : color (rgb) and roughness (a).
      RvkPassFlags_Color2 |                           // Attachment color2 (linear): normal (rgb) and tags (a).
      RvkPassFlags_DepthOutput;                       // Attachment depth.
    renderer->passes[RvkRenderPass_Geometry] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, string_lit("geometry"));
  }
  {
    const RvkPassFlags flags = RvkPassFlags_ClearColor |
      RvkPassFlags_Color1 | RvkPassFlags_SrgbColor1 | // Attachment color1 (srgb): color (rgb).
      RvkPassFlags_ExternalDepth;                     // Attachment depth.
    renderer->passes[RvkRenderPass_Forward] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, string_lit("forward"));
  }
  {
    const RvkPassFlags flags = RvkPassFlags_ClearDepth |
      RvkPassFlags_DepthOutput;                       // Attachment depth.
    renderer->passes[RvkRenderPass_Shadow] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, string_lit("shadow"));
  }
  {
    const RvkPassFlags flags = RvkPassFlags_ClearColor |
      RvkPassFlags_Color1 | RvkPassFlags_SrgbColor1;  // Attachment color1 (srgb): occlusion (r).
    renderer->passes[RvkRenderPass_AmbientOcclusion] = rvk_pass_create(dev, vkDrawBuffer, uniformPool, stopwatch, flags, string_lit("ambient-occlusion"));
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
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreBegin, &rend->dev->vkAlloc);
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreDone, &rend->dev->vkAlloc);
  vkDestroyFence(rend->dev->vkDev, rend->fenceRenderDone, &rend->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

VkSemaphore rvk_renderer_semaphore_begin(RvkRenderer* rend) { return rend->semaphoreBegin; }
VkSemaphore rvk_renderer_semaphore_done(RvkRenderer* rend) { return rend->semaphoreDone; }

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  const TimeSteady waitStart = time_steady_clock();

  rvk_call(vkWaitForFences, rend->dev->vkDev, 1, &rend->fenceRenderDone, true, u64_max);

  ((RvkRenderer*)rend)->waitForRenderDur += time_steady_duration(waitStart, time_steady_clock());
}

RvkRenderStats rvk_renderer_stats(const RvkRenderer* rend) {
  rvk_renderer_wait_for_done(rend);

  const u64 timestampBegin = rvk_stopwatch_query(rend->stopwatch, rend->timeRecBegin);
  const u64 timestampEnd   = rvk_stopwatch_query(rend->stopwatch, rend->timeRecEnd);

  RvkRenderStats result;
  result.resolution       = rend->currentResolution;
  result.renderDur        = time_nanoseconds(timestampEnd - timestampBegin);
  result.waitForRenderDur = rend->waitForRenderDur;

  for (RvkRenderPass passIdx = 0; passIdx != RvkRenderPass_Count; ++passIdx) {
    const RvkPass* pass = rend->passes[passIdx];
    if (rvk_pass_recorded(pass)) {
      result.passes[passIdx] = (RendStatPass){
          .dur         = rvk_pass_duration(pass),
          .draws       = (u32)rvk_pass_stat(pass, RvkStat_Draws),
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

void rvk_renderer_begin(
    RvkRenderer*            rend,
    const RendSettingsComp* settings,
    RvkImage*               target,
    const RvkImagePhase     targetPhase) {
  diag_assert_msg(!(rend->flags & RvkRenderer_Active), "Renderer already active");

  rend->flags |= RvkRenderer_Active;
  rend->currentTarget      = target;
  rend->currentTargetPhase = targetPhase;
  rend->currentResolution  = rvk_size_scale(target->size, settings->resolutionScale);
  rend->waitForRenderDur   = 0;

  rvk_renderer_wait_for_done(rend);
  rvk_uniform_reset(rend->uniformPool);
  rvk_commandpool_reset(rend->dev, rend->vkCmdPool);

  rvk_commandbuffer_begin(rend->vkDrawBuffer);
  rvk_stopwatch_reset(rend->stopwatch, rend->vkDrawBuffer);

  const RvkSize shadowResolution = {settings->shadowResolution, settings->shadowResolution};

  rvk_pass_setup(rend->passes[RvkRenderPass_Geometry], rend->currentResolution);
  rvk_pass_setup(rend->passes[RvkRenderPass_Forward], rend->currentResolution);
  rvk_pass_setup(rend->passes[RvkRenderPass_Shadow], shadowResolution);

  rend->timeRecBegin = rvk_stopwatch_mark(rend->stopwatch, rend->vkDrawBuffer);
  rvk_debug_label_begin(
      rend->dev->debug,
      rend->vkDrawBuffer,
      geo_color_teal,
      "renderer_{}",
      fmt_int(rend->rendererId));
}

RvkPass* rvk_renderer_pass(RvkRenderer* rend, const RvkRenderPass pass) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");
  diag_assert(pass < RvkRenderPass_Count);
  return rend->passes[pass];
}

void rvk_renderer_end(RvkRenderer* rend) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");
  array_for_t(rend->passes, RvkPassPtr, itr) {
    diag_assert_msg(
        !rvk_pass_active(*itr), "Pass '{}' is still active", fmt_text(rvk_pass_name(*itr)));
  }

  rvk_renderer_blit_to_output(rend, rend->passes[RvkRenderPass_Forward]);

  rend->timeRecEnd = rvk_stopwatch_mark(rend->stopwatch, rend->vkDrawBuffer);
  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->fenceRenderDone);
  rvk_renderer_submit(rend);

  rend->flags &= ~RvkRenderer_Active;
  rend->currentTarget = null;
}
