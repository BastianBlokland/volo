#include "core_alloc.h"
#include "core_diag.h"
#include "rend_size.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "renderer_internal.h"
#include "technique_internal.h"

struct sRvkRenderer {
  RvkDevice*      dev;
  RvkTechnique*   tech;
  RvkSwapchain*   swapchain;
  VkSemaphore     imageAvailable, imageReady;
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
      .pWaitSemaphores      = &rend->imageAvailable,
      .pWaitDstStageMask    = &waitStage,
      .commandBufferCount   = 1,
      .pCommandBuffers      = &rend->vkDrawBuffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &rend->imageReady,
  };
  rvk_call(vkQueueSubmit, rend->dev->vkGraphicsQueue, 1, &submitInfo, rend->renderDone);
}

static void rvk_viewport_set(VkCommandBuffer vkCmdBuf, const RendSize size) {
  VkViewport viewport = {
      .x        = 0.0f,
      .y        = 0.0f,
      .width    = (f32)size.width,
      .height   = (f32)size.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(vkCmdBuf, 0, 1, &viewport);
}

static void rvk_scissor_set(VkCommandBuffer vkCmdBuf, const RendSize size) {
  VkRect2D scissor = {
      .offset        = {0, 0},
      .extent.width  = size.width,
      .extent.height = size.height,
  };
  vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);
  *renderer             = (RvkRenderer){
      .dev            = dev,
      .tech           = rvk_technique_create(dev),
      .swapchain      = swapchain,
      .imageAvailable = rvk_semaphore_create(dev),
      .imageReady     = rvk_semaphore_create(dev),
      .renderDone     = rvk_fence_create(dev, true),
      .vkDrawBuffer   = rvk_commandbuffer_create(dev),
  };
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  rvk_technique_destroy(rend->tech);

  vkFreeCommandBuffers(rend->dev->vkDev, rend->dev->vkGraphicsCommandPool, 1, &rend->vkDrawBuffer);
  vkDestroySemaphore(rend->dev->vkDev, rend->imageAvailable, &rend->dev->vkAlloc);
  vkDestroySemaphore(rend->dev->vkDev, rend->imageReady, &rend->dev->vkAlloc);
  vkDestroyFence(rend->dev->vkDev, rend->renderDone, &rend->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

VkSemaphore rvk_renderer_image_available(RvkRenderer* rend) { return rend->imageAvailable; }

VkSemaphore rvk_renderer_image_ready(RvkRenderer* rend) { return rend->imageReady; }

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  rvk_call(vkWaitForFences, rend->dev->vkDev, 1, &rend->renderDone, true, u64_max);
}

void rvk_renderer_draw_begin(
    RvkRenderer* rend, const RvkSwapchainIdx swapchainIdx, const RendColor clearColor) {

  rvk_renderer_wait_for_done(rend);
  rvk_commandbuffer_begin(rend->vkDrawBuffer);

  RvkImage* targetImage = rvk_swapchain_image(rend->swapchain, swapchainIdx);
  rvk_technique_begin(rend->tech, rend->vkDrawBuffer, targetImage->size, clearColor);

  rvk_viewport_set(rend->vkDrawBuffer, targetImage->size);
  rvk_scissor_set(rend->vkDrawBuffer, targetImage->size);
}

void rvk_renderer_draw_inst(RvkRenderer* rend, RvkGraphic* graphic) {

  if (!rvk_graphic_prepare(graphic, rend->tech)) {
    return;
  }

  rvk_graphic_bind(graphic, rend->vkDrawBuffer);

  const u32 indexCount = rvk_graphic_index_count(graphic);
  vkCmdDrawIndexed(rend->vkDrawBuffer, indexCount, 1, 0, 0, 0);
}

void rvk_renderer_draw_end(RvkRenderer* rend, const RvkSwapchainIdx swapchainIdx) {

  rvk_technique_end(rend->tech, rend->vkDrawBuffer);

  // Wait for rendering to be done.
  rvk_technique_output_barrier(rend->tech, rend->vkDrawBuffer);

  // Copy the output to the swapchain.
  RvkImage* srcImage  = rvk_technique_output(rend->tech);
  RvkImage* destImage = rvk_swapchain_image(rend->swapchain, swapchainIdx);
  rvk_image_transition(destImage, rend->vkDrawBuffer, RvkImagePhase_TransferDest);
  rvk_image_blit(srcImage, destImage, rend->vkDrawBuffer);
  rvk_image_transition(destImage, rend->vkDrawBuffer, RvkImagePhase_Present);

  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->dev->vkDev, 1, &rend->renderDone);
  rvk_renderer_submit(rend);
}
