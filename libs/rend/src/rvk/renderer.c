#include "core_alloc.h"
#include "core_diag.h"
#include "rend_size.h"

#include "device_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "renderer_internal.h"

typedef enum {
  RvkRendererFlags_Active = 1 << 0,
} RvkRendererFlags;

struct sRvkRenderer {
  RvkDevice*       dev;
  RvkPass*         forwardPass;
  VkSemaphore      semaphoreBegin, semaphoreDone;
  VkFence          fenceRenderDone;
  VkCommandBuffer  vkDrawBuffer;
  RvkRendererFlags flags;
  RvkImage*        currentTarget;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call(vkCreateFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandBuffer rvk_commandbuffer_create(RvkDevice* dev) {
  VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = dev->vkGraphicsCommandPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer result;
  rvk_call(vkAllocateCommandBuffers, dev->vkDev, &allocInfo, &result);
  return result;
}

static void rvk_commandbuffer_begin(VkCommandBuffer vkCmdBuf) {
  VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call(vkBeginCommandBuffer, vkCmdBuf, &beginInfo);
}

static void rvk_commandbuffer_end(VkCommandBuffer vkCmdBuf) {
  rvk_call(vkEndCommandBuffer, vkCmdBuf);
}

static void rvk_renderer_submit(RvkRenderer* rend) {
  const VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
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
  rvk_call(vkQueueSubmit, rend->dev->vkGraphicsQueue, 1, &submitInfo, rend->fenceRenderDone);
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);
  *renderer             = (RvkRenderer){
      .dev             = dev,
      .semaphoreBegin  = rvk_semaphore_create(dev),
      .semaphoreDone   = rvk_semaphore_create(dev),
      .fenceRenderDone = rvk_fence_create(dev, true),
      .vkDrawBuffer    = rvk_commandbuffer_create(dev),
  };
  renderer->forwardPass = rvk_pass_create(dev, renderer->vkDrawBuffer);
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  rvk_renderer_wait_for_done(rend);

  rvk_pass_destroy(rend->forwardPass);

  vkFreeCommandBuffers(rend->dev->vkDev, rend->dev->vkGraphicsCommandPool, 1, &rend->vkDrawBuffer);
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreBegin, &rend->dev->vkAlloc);
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreDone, &rend->dev->vkAlloc);
  vkDestroyFence(rend->dev->vkDev, rend->fenceRenderDone, &rend->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

VkSemaphore rvk_renderer_semaphore_begin(RvkRenderer* rend) { return rend->semaphoreBegin; }
VkSemaphore rvk_renderer_semaphore_done(RvkRenderer* rend) { return rend->semaphoreDone; }

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  rvk_call(vkWaitForFences, rend->dev->vkDev, 1, &rend->fenceRenderDone, true, u64_max);
}

void rvk_renderer_begin(RvkRenderer* rend, RvkImage* target) {
  diag_assert_msg(!(rend->flags & RvkRendererFlags_Active), "Renderer already active");

  rend->flags |= RvkRendererFlags_Active;
  rend->currentTarget = target;

  rvk_pass_setup(rend->forwardPass, target->size);

  rvk_renderer_wait_for_done(rend);
  rvk_commandbuffer_begin(rend->vkDrawBuffer);
}

RvkPass* rvk_renderer_pass_forward(RvkRenderer* rend) {
  diag_assert_msg(rend->flags & RvkRendererFlags_Active, "Renderer not active");
  return rend->forwardPass;
}

void rvk_renderer_end(RvkRenderer* rend) {
  diag_assert_msg(rend->flags & RvkRendererFlags_Active, "Renderer not active");

  diag_assert_msg(!rvk_pass_active(rend->forwardPass), "Forward pass is still active");

  // Wait for rendering to be done.
  rvk_pass_output_barrier(rend->forwardPass);

  // Copy the output to the target.
  rvk_image_transition(rend->currentTarget, rend->vkDrawBuffer, RvkImagePhase_TransferDest);
  rvk_image_blit(rvk_pass_output(rend->forwardPass), rend->currentTarget, rend->vkDrawBuffer);
  rvk_image_transition(rend->currentTarget, rend->vkDrawBuffer, RvkImagePhase_Present);

  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->fenceRenderDone);
  rvk_renderer_submit(rend);

  rend->flags &= ~RvkRendererFlags_Active;
  rend->currentTarget = null;
}
