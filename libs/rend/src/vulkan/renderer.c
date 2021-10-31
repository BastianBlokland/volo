#include "core_alloc.h"
#include "gap_vector.h"

#include "renderer_internal.h"

struct sRendVkRenderer {
  RendVkDevice*    device;
  RendVkSwapchain* swapchain;
  VkSemaphore      imageAvailable, imageReady;
  VkFence          renderDone;
  VkCommandBuffer  vkDrawBuffer;
};

static VkSemaphore rend_vk_semaphore_create(RendVkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rend_vk_call(vkCreateSemaphore, dev->vkDevice, &semaphoreInfo, dev->vkAllocHost, &result);
  return result;
}

static VkFence rend_vk_fence_create(RendVkDevice* dev, const bool initialState) {
  VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rend_vk_call(vkCreateFence, dev->vkDevice, &fenceInfo, dev->vkAllocHost, &result);
  return result;
}

static void rend_vk_renderer_wait_for_done(const RendVkRenderer* renderer) {
  rend_vk_call(
      vkWaitForFences, renderer->device->vkDevice, 1, &renderer->renderDone, true, u64_max);
}

static VkCommandBuffer rend_vk_commandbuffer_create(RendVkDevice* dev) {
  VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = dev->vkMainCommandPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer result;
  rend_vk_call(vkAllocateCommandBuffers, dev->vkDevice, &allocInfo, &result);
  return result;
}

static void rend_vk_commandbuffer_begin(VkCommandBuffer vkCommandBuffer) {
  VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rend_vk_call(vkBeginCommandBuffer, vkCommandBuffer, &beginInfo);
}

static void rend_vk_commandbuffer_end(VkCommandBuffer vkCommandBuffer) {
  rend_vk_call(vkEndCommandBuffer, vkCommandBuffer);
}

static void rend_vk_renderer_submit(RendVkRenderer* renderer) {
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
  rend_vk_call(vkQueueSubmit, renderer->device->vkMainQueue, 1, &submitInfo, renderer->renderDone);
}

static void rend_vk_viewport_set(VkCommandBuffer vkCommandBuffer, const GapVector size) {
  VkViewport viewport = {
      .x        = 0.0f,
      .y        = 0.0f,
      .width    = (float)size.x,
      .height   = (float)size.y,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(vkCommandBuffer, 0, 1, &viewport);
}

static void rend_vk_scissor_set(VkCommandBuffer vkCommandBuffer, const GapVector size) {
  VkRect2D scissor = {
      .offset        = {0, 0},
      .extent.width  = size.x,
      .extent.height = size.y,
  };
  vkCmdSetScissor(vkCommandBuffer, 0, 1, &scissor);
}

RendVkRenderer* rend_vk_renderer_create(RendVkDevice* dev, RendVkSwapchain* swapchain) {
  RendVkRenderer* renderer = alloc_alloc_t(g_alloc_heap, RendVkRenderer);
  *renderer                = (RendVkRenderer){
      .device         = dev,
      .swapchain      = swapchain,
      .imageAvailable = rend_vk_semaphore_create(dev),
      .imageReady     = rend_vk_semaphore_create(dev),
      .renderDone     = rend_vk_fence_create(dev, true),
      .vkDrawBuffer   = rend_vk_commandbuffer_create(dev),
  };
  return renderer;
}

void rend_vk_renderer_destroy(RendVkRenderer* renderer) {
  vkFreeCommandBuffers(
      renderer->device->vkDevice, renderer->device->vkMainCommandPool, 1, &renderer->vkDrawBuffer);
  vkDestroySemaphore(
      renderer->device->vkDevice, renderer->imageAvailable, renderer->device->vkAllocHost);
  vkDestroySemaphore(
      renderer->device->vkDevice, renderer->imageReady, renderer->device->vkAllocHost);
  vkDestroyFence(renderer->device->vkDevice, renderer->renderDone, renderer->device->vkAllocHost);

  alloc_free_t(g_alloc_heap, renderer);
}

VkSemaphore rend_vk_renderer_image_available(RendVkRenderer* renderer) {
  return renderer->imageAvailable;
}

VkSemaphore rend_vk_renderer_image_ready(RendVkRenderer* renderer) { return renderer->imageReady; }

void rend_vk_renderer_draw_begin(RendVkRenderer* renderer, const RendSwapchainIdx swapchainIdx) {

  rend_vk_renderer_wait_for_done(renderer);
  rend_vk_commandbuffer_begin(renderer->vkDrawBuffer);

  RendVkImage* targetImage = rend_vk_swapchain_image(renderer->swapchain, swapchainIdx);
  rend_vk_viewport_set(renderer->vkDrawBuffer, targetImage->size);
  rend_vk_scissor_set(renderer->vkDrawBuffer, targetImage->size);
}

void rend_vk_renderer_draw_end(RendVkRenderer* renderer) {

  rend_vk_commandbuffer_end(renderer->vkDrawBuffer);

  rend_vk_call(vkResetFences, renderer->device->vkDevice, 1, &renderer->renderDone);
  rend_vk_renderer_submit(renderer);
}
