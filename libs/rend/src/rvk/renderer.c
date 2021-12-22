#include "core_alloc.h"
#include "core_diag.h"
#include "rend_size.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "renderer_internal.h"

struct sRvkRenderer {
  RvkDevice*      dev;
  RvkPass*        pass;
  RvkSwapchain*   swapchain;
  VkSemaphore     semaphoreBegin, semaphoreDone;
  VkFence         renderDone;
  VkCommandBuffer vkDrawBuffer;
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
  rvk_call(vkQueueSubmit, rend->dev->vkGraphicsQueue, 1, &submitInfo, rend->renderDone);
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);
  *renderer             = (RvkRenderer){
      .dev            = dev,
      .pass           = rvk_pass_create(dev),
      .swapchain      = swapchain,
      .semaphoreBegin = rvk_semaphore_create(dev),
      .semaphoreDone  = rvk_semaphore_create(dev),
      .renderDone     = rvk_fence_create(dev, true),
      .vkDrawBuffer   = rvk_commandbuffer_create(dev),
  };
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  rvk_pass_destroy(rend->pass);

  vkFreeCommandBuffers(rend->dev->vkDev, rend->dev->vkGraphicsCommandPool, 1, &rend->vkDrawBuffer);
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreBegin, &rend->dev->vkAlloc);
  vkDestroySemaphore(rend->dev->vkDev, rend->semaphoreDone, &rend->dev->vkAlloc);
  vkDestroyFence(rend->dev->vkDev, rend->renderDone, &rend->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

VkSemaphore rvk_renderer_semaphore_begin(RvkRenderer* rend) { return rend->semaphoreBegin; }
VkSemaphore rvk_renderer_semaphore_done(RvkRenderer* rend) { return rend->semaphoreDone; }

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  rvk_call(vkWaitForFences, rend->dev->vkDev, 1, &rend->renderDone, true, u64_max);
}

void rvk_renderer_draw_begin(
    RvkRenderer* rend, const RvkSwapchainIdx swapchainIdx, const RendColor clearColor) {

  rvk_renderer_wait_for_done(rend);
  rvk_commandbuffer_begin(rend->vkDrawBuffer);

  RvkImage* targetImage = rvk_swapchain_image(rend->swapchain, swapchainIdx);
  rvk_pass_begin(rend->pass, rend->vkDrawBuffer, targetImage->size, clearColor);
}

void rvk_renderer_draw_inst(RvkRenderer* rend, RvkGraphic* graphic) {

  if (!rvk_graphic_prepare(graphic, rend->pass)) {
    return;
  }

  rvk_graphic_bind(graphic, rend->vkDrawBuffer);

  const u32 indexCount = rvk_graphic_index_count(graphic);
  vkCmdDrawIndexed(rend->vkDrawBuffer, indexCount, 1, 0, 0, 0);
}

void rvk_renderer_draw_end(RvkRenderer* rend, const RvkSwapchainIdx swapchainIdx) {

  rvk_pass_end(rend->pass, rend->vkDrawBuffer);

  // Wait for rendering to be done.
  rvk_pass_output_barrier(rend->pass, rend->vkDrawBuffer);

  // Copy the output to the swapchain.
  RvkImage* srcImage  = rvk_pass_output(rend->pass);
  RvkImage* destImage = rvk_swapchain_image(rend->swapchain, swapchainIdx);
  rvk_image_transition(destImage, rend->vkDrawBuffer, RvkImagePhase_TransferDest);
  rvk_image_blit(srcImage, destImage, rend->vkDrawBuffer);
  rvk_image_transition(destImage, rend->vkDrawBuffer, RvkImagePhase_Present);

  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->renderDone);
  rvk_renderer_submit(rend);
}
