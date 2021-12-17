#include "core_alloc.h"
#include "core_diag.h"
#include "rend_size.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "renderer_internal.h"
#include "technique_internal.h"

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
  const VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
  rvk_call(vkQueueSubmit, rend->device->vkGraphicsQueue, 1, &submitInfo, rend->renderDone);
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
      .device         = dev,
      .swapchain      = swapchain,
      .imageAvailable = rvk_semaphore_create(dev),
      .imageReady     = rvk_semaphore_create(dev),
      .renderDone     = rvk_fence_create(dev, true),
      .vkDrawBuffer   = rvk_commandbuffer_create(dev),
  };
  return renderer;
}

void rvk_renderer_destroy(RvkRenderer* rend) {
  vkFreeCommandBuffers(
      rend->device->vkDev, rend->device->vkGraphicsCommandPool, 1, &rend->vkDrawBuffer);
  vkDestroySemaphore(rend->device->vkDev, rend->imageAvailable, &rend->device->vkAlloc);
  vkDestroySemaphore(rend->device->vkDev, rend->imageReady, &rend->device->vkAlloc);
  vkDestroyFence(rend->device->vkDev, rend->renderDone, &rend->device->vkAlloc);

  alloc_free_t(g_alloc_heap, rend);
}

VkSemaphore rvk_renderer_image_available(RvkRenderer* rend) { return rend->imageAvailable; }

VkSemaphore rvk_renderer_image_ready(RvkRenderer* rend) { return rend->imageReady; }

void rvk_renderer_wait_for_done(const RvkRenderer* rend) {
  rvk_call(vkWaitForFences, rend->device->vkDev, 1, &rend->renderDone, true, u64_max);
}

void rvk_renderer_draw_begin(
    RvkRenderer*          rend,
    RvkTechnique*         technique,
    const RvkSwapchainIdx swapchainIdx,
    const RendColor       clearColor) {

  rvk_renderer_wait_for_done(rend);
  rvk_commandbuffer_begin(rend->vkDrawBuffer);

  rvk_technique_begin(technique, rend->vkDrawBuffer, swapchainIdx, clearColor);

  RvkImage* targetImage = rvk_swapchain_image(rend->swapchain, swapchainIdx);
  rvk_viewport_set(rend->vkDrawBuffer, targetImage->size);
  rvk_scissor_set(rend->vkDrawBuffer, targetImage->size);
}

void rvk_renderer_draw_inst(RvkRenderer* rend, RvkGraphic* graphic) {

  diag_assert_msg(graphic->vkPipeline, "Graphic not initialized");
  vkCmdBindPipeline(rend->vkDrawBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic->vkPipeline);
  vkCmdDraw(rend->vkDrawBuffer, 3, 1, 0, 0);
}

void rvk_renderer_draw_end(RvkRenderer* rend, RvkTechnique* tech) {

  rvk_technique_end(tech, rend->vkDrawBuffer);
  rvk_commandbuffer_end(rend->vkDrawBuffer);

  rvk_call(vkResetFences, rend->device->vkDev, 1, &rend->renderDone);
  rvk_renderer_submit(rend);
}
