#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"

#include "canvas_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "job_internal.h"
#include "pass_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "uniform_internal.h"

typedef RvkPass* RvkPassPtr;

typedef enum {
  RvkJob_Active = 1 << 0,
} RvkJobFlags;

struct sRvkJob {
  RvkDevice*      dev;
  u32             jobId;
  RvkUniformPool* uniformPool;
  RvkStopwatch*   stopwatch;

  /**
   * Passes are stored per-job as they contain state that needs to persist throughout the lifetime
   * of the submission.
   */
  RvkPass* passes[RendPass_Count];

  VkFence         fenceJobDone;
  VkCommandPool   vkCmdPool;
  VkCommandBuffer vkDrawBuffer;
  RvkJobFlags     flags;

  RvkStopwatchRecord timeRecBegin, timeRecEnd;
  TimeDuration       waitForGpuDur;
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

static void rvk_job_submit(
    RvkJob*            job,
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

  const VkCommandBuffer commandBuffers[] = {job->vkDrawBuffer};

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
  thread_mutex_lock(job->dev->queueSubmitMutex);
  rvk_call(
      vkQueueSubmit,
      job->dev->vkGraphicsQueue,
      array_elems(submitInfos),
      submitInfos,
      job->fenceJobDone);
  thread_mutex_unlock(job->dev->queueSubmitMutex);
}

RvkJob* rvk_job_create(
    RvkDevice*           dev,
    const VkFormat       swapchainFormat,
    const u32            jobId,
    const RvkPassConfig* passConfig /* RvkPassConfig[RendPass_Count] */) {
  RvkJob* job = alloc_alloc_t(g_allocHeap, RvkJob);

  RvkUniformPool* uniformPool = rvk_uniform_pool_create(dev);
  RvkStopwatch*   stopwatch   = rvk_stopwatch_create(dev);

  VkCommandPool vkCmdPool = rvk_commandpool_create(dev, dev->graphicsQueueIndex);
  rvk_debug_name_cmdpool(dev->debug, vkCmdPool, "job_{}", fmt_int(jobId));

  VkCommandBuffer vkDrawBuffer = rvk_commandbuffer_create(dev, vkCmdPool);

  *job = (RvkJob){
      .dev          = dev,
      .uniformPool  = uniformPool,
      .stopwatch    = stopwatch,
      .jobId        = jobId,
      .fenceJobDone = rvk_fence_create(dev, true),
      .vkCmdPool    = vkCmdPool,
      .vkDrawBuffer = vkDrawBuffer,
  };

  for (RendPass pass = 0; pass != RendPass_Count; ++pass) {
    job->passes[pass] = rvk_pass_create(
        dev,
        swapchainFormat,
        vkDrawBuffer,
        uniformPool,
        stopwatch,
        passConfig[pass],
        rend_pass_name(pass));
  }

  return job;
}

void rvk_job_destroy(RvkJob* job) {
  rvk_job_wait_for_done(job);

  array_for_t(job->passes, RvkPassPtr, itr) { rvk_pass_destroy(*itr); }

  rvk_uniform_pool_destroy(job->uniformPool);
  rvk_stopwatch_destroy(job->stopwatch);

  vkDestroyCommandPool(job->dev->vkDev, job->vkCmdPool, &job->dev->vkAlloc);
  vkDestroyFence(job->dev->vkDev, job->fenceJobDone, &job->dev->vkAlloc);

  alloc_free_t(g_allocHeap, job);
}

void rvk_job_wait_for_done(const RvkJob* job) {
  const TimeSteady waitStart = time_steady_clock();

  rvk_call(vkWaitForFences, job->dev->vkDev, 1, &job->fenceJobDone, true, u64_max);

  ((RvkJob*)job)->waitForGpuDur += time_steady_duration(waitStart, time_steady_clock());
}

RvkCanvasStats rvk_job_stats(const RvkJob* job) {
  rvk_job_wait_for_done(job);

  const TimeSteady timestampBegin = rvk_stopwatch_query(job->stopwatch, job->timeRecBegin);
  const TimeSteady timestampEnd   = rvk_stopwatch_query(job->stopwatch, job->timeRecEnd);

  RvkCanvasStats result;
  result.waitForGpuDur = job->waitForGpuDur;
  result.gpuExecDur    = time_steady_duration(timestampBegin, timestampEnd);

  for (RendPass passIdx = 0; passIdx != RendPass_Count; ++passIdx) {
    const RvkPass* pass    = job->passes[passIdx];
    const RvkSize  sizeMax = rvk_pass_stat_size_max(pass);
    result.passes[passIdx] = (RendStatPass){
        .gpuExecDur  = rvk_pass_stat_duration(pass),
        .sizeMax[0]  = sizeMax.width,
        .sizeMax[1]  = sizeMax.height,
        .invocations = rvk_pass_stat_invocations(pass),
        .draws       = rvk_pass_stat_draws(pass),
        .instances   = rvk_pass_stat_instances(pass),
        .vertices    = rvk_pass_stat_pipeline(pass, RvkStat_InputAssemblyVertices),
        .primitives  = rvk_pass_stat_pipeline(pass, RvkStat_InputAssemblyPrimitives),
        .shadersVert = rvk_pass_stat_pipeline(pass, RvkStat_ShaderInvocationsVert),
        .shadersFrag = rvk_pass_stat_pipeline(pass, RvkStat_ShaderInvocationsFrag),
    };
  }

  return result;
}

void rvk_job_begin(RvkJob* job) {
  diag_assert_msg(!(job->flags & RvkJob_Active), "job already active");

  job->flags |= RvkJob_Active;
  job->waitForGpuDur = 0;

  rvk_job_wait_for_done(job);
  rvk_uniform_reset(job->uniformPool);
  rvk_commandpool_reset(job->dev, job->vkCmdPool);

  rvk_commandbuffer_begin(job->vkDrawBuffer);
  rvk_stopwatch_reset(job->stopwatch, job->vkDrawBuffer);

  array_for_t(job->passes, RvkPassPtr, itr) { rvk_pass_reset(*itr); }

  job->timeRecBegin = rvk_stopwatch_mark(job->stopwatch, job->vkDrawBuffer);
  rvk_debug_label_begin(
      job->dev->debug, job->vkDrawBuffer, geo_color_teal, "job_{}", fmt_int(job->jobId));
}

RvkPass* rvk_job_pass(RvkJob* job, const RendPass pass) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  diag_assert(pass < RendPass_Count);
  return job->passes[pass];
}

void rvk_job_img_clear_color(RvkJob* job, RvkImage* img, const GeoColor color) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  rvk_debug_label_begin(job->dev->debug, job->vkDrawBuffer, geo_color_purple, "clear-color");

  rvk_image_transition(img, RvkImagePhase_TransferDest, job->vkDrawBuffer);
  rvk_image_clear_color(img, color, job->vkDrawBuffer);

  rvk_debug_label_end(job->dev->debug, job->vkDrawBuffer);
}

void rvk_job_img_clear_depth(RvkJob* job, RvkImage* img, const f32 depth) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  rvk_debug_label_begin(job->dev->debug, job->vkDrawBuffer, geo_color_purple, "clear-depth");

  rvk_image_transition(img, RvkImagePhase_TransferDest, job->vkDrawBuffer);
  rvk_image_clear_depth(img, depth, job->vkDrawBuffer);

  rvk_debug_label_end(job->dev->debug, job->vkDrawBuffer);
}

void rvk_job_img_copy(RvkJob* job, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  rvk_debug_label_begin(job->dev->debug, job->vkDrawBuffer, geo_color_purple, "copy");

  const RvkImageTransition transitions[] = {
      {.img = src, .phase = RvkImagePhase_TransferSource},
      {.img = dst, .phase = RvkImagePhase_TransferDest},
  };
  rvk_image_transition_batch(transitions, array_elems(transitions), job->vkDrawBuffer);

  rvk_image_copy(src, dst, job->vkDrawBuffer);

  rvk_debug_label_end(job->dev->debug, job->vkDrawBuffer);
}

void rvk_job_img_blit(RvkJob* job, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  rvk_debug_label_begin(job->dev->debug, job->vkDrawBuffer, geo_color_purple, "blit");

  const RvkImageTransition transitions[] = {
      {.img = src, .phase = RvkImagePhase_TransferSource},
      {.img = dst, .phase = RvkImagePhase_TransferDest},
  };
  rvk_image_transition_batch(transitions, array_elems(transitions), job->vkDrawBuffer);

  rvk_image_blit(src, dst, job->vkDrawBuffer);

  rvk_debug_label_end(job->dev->debug, job->vkDrawBuffer);
}

void rvk_job_img_transition(RvkJob* job, RvkImage* img, const RvkImagePhase targetPhase) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  rvk_image_transition(img, targetPhase, job->vkDrawBuffer);
}

void rvk_job_barrier_full(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  const VkMemoryBarrier barrier = {
      .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR,
  };
  const VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
  const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
  vkCmdPipelineBarrier(job->vkDrawBuffer, srcStage, dstStage, 0, 1, &barrier, 0, null, 0, null);
}

void rvk_job_end(
    RvkJob*            job,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  array_for_t(job->passes, RvkPassPtr, itr) {
    diag_assert_msg(
        !rvk_pass_active(*itr), "Pass '{}' is still active", fmt_text(rvk_pass_name(*itr)));
  }

  job->timeRecEnd = rvk_stopwatch_mark(job->stopwatch, job->vkDrawBuffer);
  rvk_debug_label_end(job->dev->debug, job->vkDrawBuffer);
  rvk_commandbuffer_end(job->vkDrawBuffer);

  rvk_call(vkResetFences, job->dev->vkDev, 1, &job->fenceJobDone);
  rvk_job_submit(job, waitForDeps, waitForTarget, signals, signalCount);

  job->flags &= ~RvkJob_Active;
}
