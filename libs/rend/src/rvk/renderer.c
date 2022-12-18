#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"

#include "device_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "renderer_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "types_internal.h"
#include "uniform_internal.h"

typedef enum {
  RvkRenderer_Active            = 1 << 0,
  RvkRenderer_SubmittedDrawOnce = 1 << 1,
} RvkRendererFlags;

struct sRvkRenderer {
  RvkDevice*       dev;
  u32              rendererId;
  RvkUniformPool*  uniformPool;
  RvkStopwatch*    stopwatch;
  RvkPass*         passGeometry;
  RvkPass*         passForward;
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
  const VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo               submitInfo = {
                    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .waitSemaphoreCount   = 1,
                    .pWaitSemaphores      = &rend->semaphoreBegin,
                    .pWaitDstStageMask    = &waitStage,
                    .commandBufferCount   = 1,
                    .pCommandBuffers      = &rend->vkDrawBuffer,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores    = &rend->semaphoreDone,
  };
  thread_mutex_lock(rend->dev->queueSubmitMutex);
  rvk_call(vkQueueSubmit, rend->dev->vkGraphicsQueue, 1, &submitInfo, rend->fenceRenderDone);
  thread_mutex_unlock(rend->dev->queueSubmitMutex);
}

static void rvk_renderer_blit_to_output(RvkRenderer* rend, RvkPass* pass) {
  rvk_debug_label_begin(rend->dev->debug, rend->vkDrawBuffer, geo_color_purple, "blit_to_output");

  RvkImage* src  = rvk_pass_output(pass, RvkPassOutput_Color);
  RvkImage* dest = rend->currentTarget;

  rvk_image_transition(src, rend->vkDrawBuffer, RvkImagePhase_TransferSource);
  rvk_image_transition(dest, rend->vkDrawBuffer, RvkImagePhase_TransferDest);

  rvk_image_blit(src, dest, rend->vkDrawBuffer);

  rvk_image_transition(dest, rend->vkDrawBuffer, rend->currentTargetPhase);

  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
}

static RvkSize rvk_renderer_resolution(RvkImage* target, const RendSettingsComp* settings) {
  return rvk_size(
      (u32)math_round_nearest_f32(target->size.width * settings->resolutionScale),
      (u32)math_round_nearest_f32(target->size.height * settings->resolutionScale));
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev, const u32 rendererId) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);
  *renderer             = (RvkRenderer){
                  .dev             = dev,
                  .uniformPool     = rvk_uniform_pool_create(dev),
                  .stopwatch       = rvk_stopwatch_create(dev),
                  .rendererId      = rendererId,
                  .semaphoreBegin  = rvk_semaphore_create(dev),
                  .semaphoreDone   = rvk_semaphore_create(dev),
                  .fenceRenderDone = rvk_fence_create(dev, true),
                  .vkCmdPool       = rvk_commandpool_create(dev, dev->graphicsQueueIndex),
  };
  rvk_debug_name_cmdpool(dev->debug, renderer->vkCmdPool, "renderer_{}", fmt_int(rendererId));
  renderer->vkDrawBuffer = rvk_commandbuffer_create(dev, renderer->vkCmdPool);
  renderer->passGeometry = rvk_pass_create(
      dev,
      renderer->vkDrawBuffer,
      renderer->uniformPool,
      RvkPassFlags_ClearColor | RvkPassFlags_Default,
      string_lit("geometry"));
  renderer->passForward = rvk_pass_create(
      dev,
      renderer->vkDrawBuffer,
      renderer->uniformPool,
      RvkPassFlags_ClearColor | RvkPassFlags_Default,
      string_lit("forward"));
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  rvk_renderer_wait_for_done(rend);

  rvk_pass_destroy(rend->passGeometry);
  rvk_pass_destroy(rend->passForward);
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
  if (!(rend->flags & RvkRenderer_SubmittedDrawOnce)) {
    // This renderer has never submitted a draw so there are no statistics.
    return (RvkRenderStats){0};
  }

  rvk_renderer_wait_for_done(rend);

  const u64 timestampBegin = rvk_stopwatch_query(rend->stopwatch, rend->timeRecBegin);
  const u64 timestampEnd   = rvk_stopwatch_query(rend->stopwatch, rend->timeRecEnd);

  return (RvkRenderStats){
      .renderDur          = time_nanoseconds(timestampEnd - timestampBegin),
      .waitForRenderDur   = rend->waitForRenderDur,
      .forwardResolution  = rend->currentResolution,
      .forwardDraws       = (u32)rvk_pass_stat(rend->passForward, RvkStat_Draws),
      .forwardInstances   = (u32)rvk_pass_stat(rend->passForward, RvkStat_Instances),
      .forwardVertices    = rvk_pass_stat(rend->passForward, RvkStat_InputAssemblyVertices),
      .forwardPrimitives  = rvk_pass_stat(rend->passForward, RvkStat_InputAssemblyPrimitives),
      .forwardShadersVert = rvk_pass_stat(rend->passForward, RvkStat_ShaderInvocationsVert),
      .forwardShadersFrag = rvk_pass_stat(rend->passForward, RvkStat_ShaderInvocationsFrag),
  };
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
  rend->currentResolution  = rvk_renderer_resolution(target, settings);
  rend->waitForRenderDur   = 0;

  rvk_renderer_wait_for_done(rend);
  rvk_uniform_reset(rend->uniformPool);
  rvk_commandpool_reset(rend->dev, rend->vkCmdPool);

  rvk_commandbuffer_begin(rend->vkDrawBuffer);
  rvk_stopwatch_reset(rend->stopwatch, rend->vkDrawBuffer);
  rvk_pass_setup(rend->passGeometry, rend->currentResolution);
  rvk_pass_setup(rend->passForward, rend->currentResolution);

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
  switch (pass) {
  case RvkRenderPass_Geometry:
    return rend->passGeometry;
  case RvkRenderPass_Forward:
    return rend->passForward;
  case RvkRenderPass_Count:
    break;
  }
  UNREACHABLE
  return null;
}

void rvk_renderer_end(RvkRenderer* rend) {
  diag_assert_msg(rend->flags & RvkRenderer_Active, "Renderer not active");
  diag_assert_msg(!rvk_pass_active(rend->passGeometry), "Geometry pass is still active");
  diag_assert_msg(!rvk_pass_active(rend->passForward), "Forward pass is still active");

  rvk_renderer_blit_to_output(rend, rend->passForward);

  rend->timeRecEnd = rvk_stopwatch_mark(rend->stopwatch, rend->vkDrawBuffer);
  rvk_debug_label_end(rend->dev->debug, rend->vkDrawBuffer);
  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->fenceRenderDone);
  rvk_renderer_submit(rend);

  rend->flags |= RvkRenderer_SubmittedDrawOnce;
  rend->flags &= ~RvkRenderer_Active;
  rend->currentTarget = null;
}
