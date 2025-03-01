#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "geo_color.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "job_internal.h"
#include "lib_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "uniform_internal.h"

typedef enum {
  RvkJob_Active = 1 << 0,
} RvkJobFlags;

struct sRvkJob {
  RvkDevice* dev;
  u32        jobId;

  RvkJobFlags flags : 16;
  RvkJobPhase phase : 16;

  RvkUniformPool*  uniformPool;
  RvkStopwatch*    stopwatch;
  RvkStatRecorder* statrecorder;

  VkFence         fenceJobDone;
  VkCommandPool   vkCmdPool;
  VkCommandBuffer vkCmdBuffers[RvkJobPhase_Count];

  RvkStopwatchRecord gpuTimeBegin, gpuTimeEnd;
  RvkStopwatchRecord gpuWaitBegin, gpuWaitEnd;
  TimeDuration       cpuWaitDur;
};

static const String g_rvkJobPhaseNames[] = {
    [RvkJobPhase_Main]   = string_static("main"),
    [RvkJobPhase_Output] = string_static("output"),
};
ASSERT(array_elems(g_rvkJobPhaseNames) == RvkJobPhase_Count, "Unexpected phase name count");

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  const VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call_checked(dev, createFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandPool rvk_commandpool_create(RvkDevice* dev, const u32 queueIndex) {
  VkCommandPoolCreateInfo createInfo = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queueIndex,
      .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
  };
  VkCommandPool result;
  rvk_call_checked(dev, createCommandPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_commandbuffer_create_batch(
    RvkDevice* dev, VkCommandPool vkCmdPool, VkCommandBuffer out[], const u32 count) {
  const VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = vkCmdPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = count,
  };
  rvk_call_checked(dev, allocateCommandBuffers, dev->vkDev, &allocInfo, out);
}

static void rvk_commandpool_reset(RvkDevice* dev, VkCommandPool vkCmdPool) {
  rvk_call_checked(dev, resetCommandPool, dev->vkDev, vkCmdPool, 0);
}

static void rvk_commandbuffer_begin(RvkDevice* dev, VkCommandBuffer vkCmdBuf) {
  const VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call_checked(dev, beginCommandBuffer, vkCmdBuf, &beginInfo);
}

static void rvk_commandbuffer_end(RvkDevice* dev, VkCommandBuffer vkCmdBuf) {
  rvk_call_checked(dev, endCommandBuffer, vkCmdBuf);
}

static void rvk_job_submit(
    RvkJob* job, VkSemaphore waitForTarget, const VkSemaphore signals[], const u32 signalCount) {

  diag_assert(job->phase == RvkJobPhase_Output);

  const VkPipelineStageFlags waitForTargetStageMask =
      VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  const VkSubmitInfo info = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount   = waitForTarget ? 1 : 0,
      .pWaitSemaphores      = &waitForTarget,
      .pWaitDstStageMask    = &waitForTargetStageMask,
      .commandBufferCount   = 1,
      .pCommandBuffers      = &job->vkCmdBuffers[job->phase],
      .signalSemaphoreCount = signalCount,
      .pSignalSemaphores    = signals,
  };
  thread_mutex_lock(job->dev->queueSubmitMutex);
  rvk_call_checked(job->dev, queueSubmit, job->dev->vkGraphicsQueue, 1, &info, job->fenceJobDone);
  thread_mutex_unlock(job->dev->queueSubmitMutex);
}

static void rvk_job_phase_begin(RvkJob* job) {
  rvk_commandbuffer_begin(job->dev, job->vkCmdBuffers[job->phase]);
  rvk_debug_label_begin(
      job->dev->debug,
      job->vkCmdBuffers[job->phase],
      geo_color_teal,
      "job_{}_{}",
      fmt_int(job->jobId),
      fmt_text(g_rvkJobPhaseNames[job->phase]));
}

static void rvk_job_phase_end(RvkJob* job) {
  rvk_debug_label_end(job->dev->debug, job->vkCmdBuffers[job->phase]);
  rvk_commandbuffer_end(job->dev, job->vkCmdBuffers[job->phase]);
}

static void rvk_job_phase_submit(RvkJob* job) {
  diag_assert(job->phase != RvkJobPhase_Output); // Output cannot be submitted individually.

  const VkSubmitInfo submitInfo = {
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers    = &job->vkCmdBuffers[job->phase],
  };
  thread_mutex_lock(job->dev->queueSubmitMutex);
  rvk_call_checked(job->dev, queueSubmit, job->dev->vkGraphicsQueue, 1, &submitInfo, null);
  thread_mutex_unlock(job->dev->queueSubmitMutex);
}

static TimeDuration rvk_job_stopwatch_duration(
    const RvkJob* job, const RvkStopwatchRecord begin, const RvkStopwatchRecord end) {
  diag_assert(rvk_job_is_done(job));

  const TimeSteady timestampBegin = rvk_stopwatch_query(job->stopwatch, begin);
  const TimeSteady timestampEnd   = rvk_stopwatch_query(job->stopwatch, end);
  return time_steady_duration(timestampBegin, timestampEnd);
}

RvkJob* rvk_job_create(RvkDevice* dev, const u32 jobId) {
  RvkJob* job = alloc_alloc_t(g_allocHeap, RvkJob);

  VkCommandPool vkCmdPool = rvk_commandpool_create(dev, dev->graphicsQueueIndex);
  rvk_debug_name_cmdpool(dev->debug, vkCmdPool, "job_{}", fmt_int(jobId));

  *job = (RvkJob){
      .dev          = dev,
      .uniformPool  = rvk_uniform_pool_create(dev),
      .stopwatch    = rvk_stopwatch_create(dev),
      .statrecorder = rvk_statrecorder_create(dev),
      .jobId        = jobId,
      .fenceJobDone = rvk_fence_create(dev, true),
      .vkCmdPool    = vkCmdPool,
  };

  rvk_commandbuffer_create_batch(dev, vkCmdPool, job->vkCmdBuffers, RvkJobPhase_Count);

  rvk_debug_name_fence(dev->debug, job->fenceJobDone, "job_{}", fmt_int(jobId));

  return job;
}

void rvk_job_destroy(RvkJob* job) {
  rvk_job_wait_for_done(job);

  rvk_uniform_pool_destroy(job->uniformPool);
  rvk_stopwatch_destroy(job->stopwatch);
  rvk_statrecorder_destroy(job->statrecorder);

  RvkDevice* dev = job->dev;
  dev->api.destroyCommandPool(dev->vkDev, job->vkCmdPool, &dev->vkAlloc);
  dev->api.destroyFence(dev->vkDev, job->fenceJobDone, &dev->vkAlloc);

  alloc_free_t(g_allocHeap, job);
}

bool rvk_job_is_done(const RvkJob* job) {
  RvkDevice*     dev         = job->dev;
  const VkResult fenceStatus = dev->api.getFenceStatus(dev->vkDev, job->fenceJobDone);
  return fenceStatus == VK_SUCCESS;
}

void rvk_job_wait_for_done(const RvkJob* job) {
  const TimeSteady waitStart = time_steady_clock();

  rvk_call_checked(job->dev, waitForFences, job->dev->vkDev, 1, &job->fenceJobDone, true, u64_max);

  ((RvkJob*)job)->cpuWaitDur += time_steady_duration(waitStart, time_steady_clock());
}

void rvk_job_stats(const RvkJob* job, RvkJobStats* out) {
  diag_assert(rvk_job_is_done(job));

  out->cpuWaitDur = job->cpuWaitDur;
  out->gpuWaitDur = rvk_job_stopwatch_duration(job, job->gpuWaitBegin, job->gpuWaitEnd);
  out->gpuExecDur = rvk_job_stopwatch_duration(job, job->gpuTimeBegin, job->gpuTimeEnd);

  // NOTE: Consider the wait-time as non-executing.
  out->gpuExecDur = math_max(out->gpuExecDur - out->gpuWaitDur, 0);
}

void rvk_job_begin(RvkJob* job, const RvkJobPhase firstPhase) {
  diag_assert(rvk_job_is_done(job));
  diag_assert_msg(!(job->flags & RvkJob_Active), "job already active");

  job->flags |= RvkJob_Active;
  job->phase      = firstPhase;
  job->cpuWaitDur = 0;

  rvk_uniform_reset(job->uniformPool);
  rvk_commandpool_reset(job->dev, job->vkCmdPool);

  rvk_job_phase_begin(job);

  rvk_stopwatch_reset(job->stopwatch, job->vkCmdBuffers[job->phase]);
  rvk_statrecorder_reset(job->statrecorder, job->vkCmdBuffers[job->phase]);

  job->gpuTimeBegin = rvk_stopwatch_mark(job->stopwatch, job->vkCmdBuffers[job->phase]);
}

RvkJobPhase rvk_job_phase(const RvkJob* job) { return job->phase; }

void rvk_job_advance(RvkJob* job) {
  diag_assert(job->phase != RvkJobPhase_Last);

  const RvkJobPhase phaseNext = job->phase + 1;
  if (phaseNext == RvkJobPhase_Last) {
    job->gpuWaitBegin = rvk_stopwatch_mark(job->stopwatch, job->vkCmdBuffers[job->phase]);
  }

  rvk_job_phase_end(job);
  rvk_job_phase_submit(job);

  job->phase = phaseNext;

  rvk_job_phase_begin(job);
  job->gpuWaitEnd = rvk_stopwatch_mark(job->stopwatch, job->vkCmdBuffers[job->phase]);
}

RvkUniformPool* rvk_job_uniform_pool(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  return job->uniformPool;
}

RvkStopwatch* rvk_job_stopwatch(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  return job->stopwatch;
}

RvkStatRecorder* rvk_job_statrecorder(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  return job->statrecorder;
}

VkCommandBuffer rvk_job_cmdbuffer(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  return job->vkCmdBuffers[job->phase];
}

Mem rvk_job_uniform_map(RvkJob* job, const RvkUniformHandle handle) {
  return rvk_uniform_map(job->uniformPool, handle);
}

RvkUniformHandle rvk_job_uniform_push(RvkJob* job, const usize size) {
  return rvk_uniform_push(job->uniformPool, size);
}

RvkUniformHandle
rvk_job_uniform_push_next(RvkJob* job, const RvkUniformHandle head, const usize size) {
  return rvk_uniform_push_next(job->uniformPool, head, size);
}

void rvk_job_img_clear_color(RvkJob* job, RvkImage* img, const GeoColor color) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];
  rvk_debug_label_begin(job->dev->debug, cmdBuf, geo_color_purple, "clear-color");

  rvk_image_transition(job->dev, img, RvkImagePhase_TransferDest, cmdBuf);
  rvk_image_clear_color(job->dev, img, color, cmdBuf);

  rvk_debug_label_end(job->dev->debug, cmdBuf);
}

void rvk_job_img_clear_depth(RvkJob* job, RvkImage* img, const f32 depth) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];
  rvk_debug_label_begin(job->dev->debug, cmdBuf, geo_color_purple, "clear-depth");

  rvk_image_transition(job->dev, img, RvkImagePhase_TransferDest, cmdBuf);
  rvk_image_clear_depth(job->dev, img, depth, cmdBuf);

  rvk_debug_label_end(job->dev->debug, cmdBuf);
}

void rvk_job_img_copy(RvkJob* job, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];
  rvk_debug_label_begin(job->dev->debug, cmdBuf, geo_color_purple, "copy");

  const RvkImageTransition transitions[] = {
      {.img = src, .phase = RvkImagePhase_TransferSource},
      {.img = dst, .phase = RvkImagePhase_TransferDest},
  };
  rvk_image_transition_batch(job->dev, transitions, array_elems(transitions), cmdBuf);

  rvk_image_copy(job->dev, src, dst, cmdBuf);

  rvk_debug_label_end(job->dev->debug, cmdBuf);
}

void rvk_job_img_blit(RvkJob* job, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];
  rvk_debug_label_begin(job->dev->debug, cmdBuf, geo_color_purple, "blit");

  const RvkImageTransition transitions[] = {
      {.img = src, .phase = RvkImagePhase_TransferSource},
      {.img = dst, .phase = RvkImagePhase_TransferDest},
  };
  rvk_image_transition_batch(job->dev, transitions, array_elems(transitions), cmdBuf);

  rvk_image_blit(job->dev, src, dst, cmdBuf);

  rvk_debug_label_end(job->dev->debug, cmdBuf);
}

void rvk_job_img_transition(RvkJob* job, RvkImage* img, const RvkImagePhase targetPhase) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];
  rvk_image_transition(job->dev, img, targetPhase, cmdBuf);
}

void rvk_job_barrier_full(RvkJob* job) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");

  VkCommandBuffer cmdBuf = job->vkCmdBuffers[job->phase];

  const VkMemoryBarrier barrier = {
      .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
  };
  const VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  job->dev->api.cmdPipelineBarrier(cmdBuf, srcStage, dstStage, 0, 1, &barrier, 0, null, 0, null);
}

void rvk_job_end(
    RvkJob* job, VkSemaphore waitForTarget, const VkSemaphore signals[], u32 signalCount) {
  diag_assert_msg(job->flags & RvkJob_Active, "job not active");
  diag_assert_msg(job->phase == RvkJobPhase_Last, "job not advanced to the last phase");

  job->gpuTimeEnd = rvk_stopwatch_mark(job->stopwatch, job->vkCmdBuffers[job->phase]);

  rvk_job_phase_end(job);
  rvk_uniform_flush(job->uniformPool);

  rvk_call_checked(job->dev, resetFences, job->dev->vkDev, 1, &job->fenceJobDone);
  rvk_job_submit(job, waitForTarget, signals, signalCount);

  job->flags &= ~RvkJob_Active;
}
