#include "core_alloc.h"
#include "rend_size.h"

#include "renderer_internal.h"

struct sRvkRenderer {
  RvkDevice*      device;
  RvkSwapchain*   swapchain;
  VkSemaphore     imageAvailable, imageReady;
  VkFence         renderDone;
  VkCommandBuffer vkDrawBuffer;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDevice, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call(vkCreateFence, dev->vkDevice, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandBuffer rvk_commandbuffer_create(RvkDevice* dev) {
  VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = dev->vkMainCommandPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer result;
  rvk_call(vkAllocateCommandBuffers, dev->vkDevice, &allocInfo, &result);
  return result;
}

static void rvk_commandbuffer_begin(VkCommandBuffer vkCommandBuffer) {
  VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call(vkBeginCommandBuffer, vkCommandBuffer, &beginInfo);
}

static void rvk_commandbuffer_end(VkCommandBuffer vkCommandBuffer) {
  rvk_call(vkEndCommandBuffer, vkCommandBuffer);
}

static void rvk_renderer_submit(RvkRenderer* renderer) {
  const VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo               submitInfo = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount   = 1,
      .pWaitSemaphores      = &renderer->imageAvailable,
      .pWaitDstStageMask    = &waitStage,
      .commandBufferCount   = 1,
      .pCommandBuffers      = &renderer->vkDrawBuffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &renderer->imageReady,
  };
  rvk_call(vkQueueSubmit, renderer->device->vkMainQueue, 1, &submitInfo, renderer->renderDone);
}

static void rvk_viewport_set(VkCommandBuffer vkCommandBuffer, const RendSize size) {
  VkViewport viewport = {
      .x        = 0.0f,
      .y        = 0.0f,
      .width    = (f32)size.width,
      .height   = (f32)size.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(vkCommandBuffer, 0, 1, &viewport);
}

static void rvk_scissor_set(VkCommandBuffer vkCommandBuffer, const RendSize size) {
  VkRect2D scissor = {
      .offset        = {0, 0},
      .extent.width  = size.width,
      .extent.height = size.height,
  };
  vkCmdSetScissor(vkCommandBuffer, 0, 1, &scissor);
}

RvkRenderer* rvk_renderer_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  RvkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RvkRenderer);
  *renderer             = (RvkRenderer){
      .device         = dev,
      .swapchain      = swapchain,
      .imageAvailable = rvk_semaphore_create(dev),
      .imageReady     = rvk_semaphore_create(dev),
      .renderDone     = rvk_fence_create(dev, true),
      .vkDrawBuffer   = rvk_commandbuffer_create(dev),
  };
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* renderer) {
  vkFreeCommandBuffers(
      renderer->device->vkDevice, renderer->device->vkMainCommandPool, 1, &renderer->vkDrawBuffer);
  vkDestroySemaphore(
      renderer->device->vkDevice, renderer->imageAvailable, &renderer->device->vkAlloc);
  vkDestroySemaphore(renderer->device->vkDevice, renderer->imageReady, &renderer->device->vkAlloc);
  vkDestroyFence(renderer->device->vkDevice, renderer->renderDone, &renderer->device->vkAlloc);

  alloc_free_t(g_alloc_heap, renderer);
}

VkSemaphore rvk_renderer_image_available(RvkRenderer* renderer) { return renderer->imageAvailable; }

VkSemaphore rvk_renderer_image_ready(RvkRenderer* renderer) { return renderer->imageReady; }

void rvk_renderer_wait_for_done(const RvkRenderer* renderer) {
  rvk_call(vkWaitForFences, renderer->device->vkDevice, 1, &renderer->renderDone, true, u64_max);
}

void rvk_renderer_draw_begin(
    RvkRenderer*          renderer,
    RvkTechnique*         technique,
    const RvkSwapchainIdx swapchainIdx,
    const RendColor       clearColor) {

  rvk_renderer_wait_for_done(renderer);
  rvk_commandbuffer_begin(renderer->vkDrawBuffer);

  rvk_technique_begin(technique, renderer->vkDrawBuffer, swapchainIdx, clearColor);

  RvkImage* targetImage = rvk_swapchain_image(renderer->swapchain, swapchainIdx);
  rvk_viewport_set(renderer->vkDrawBuffer, targetImage->size);
  rvk_scissor_set(renderer->vkDrawBuffer, targetImage->size);
}

void rvk_renderer_draw_end(RvkRenderer* renderer, RvkTechnique* technique) {

  rvk_technique_end(technique, renderer->vkDrawBuffer);
  rvk_commandbuffer_end(renderer->vkDrawBuffer);

  rvk_call(vkResetFences, renderer->device->vkDevice, 1, &renderer->renderDone);
  rvk_renderer_submit(renderer);
}
