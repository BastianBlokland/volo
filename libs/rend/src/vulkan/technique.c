#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

#include "technique_internal.h"

#define attachment_max 8

struct sRendVkTechnique {
  RendVkDevice*    device;
  RendVkSwapchain* swapchain;
  VkRenderPass     vkRenderPass;
  u64              swapchainVersion;
  DynArray         frameBuffers; // VkFramebuffer[]
};

static VkRenderPass rend_vk_renderpass_create(RendVkDevice* dev, RendVkSwapchain* swapchain) {
  VkAttachmentDescription attachments[attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[attachment_max];
  u32                     colorRefCount = 0;

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = rend_vk_swapchain_format(swapchain),
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  colorRefs[colorRefCount++] = (VkAttachmentReference){
      .attachment = attachmentCount - 1,
      .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpass = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = colorRefCount,
      .pColorAttachments    = colorRefs,
  };
  VkSubpassDependency dependency = {
      .srcSubpass    = VK_SUBPASS_EXTERNAL,
      .dstSubpass    = 0,
      .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  VkRenderPassCreateInfo renderPassInfo = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachmentCount,
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = 1,
      .pDependencies   = &dependency,
  };
  VkRenderPass result;
  rend_vk_call(vkCreateRenderPass, dev->vkDevice, &renderPassInfo, dev->vkAllocHost, &result);
  return result;
}

static VkFramebuffer
rend_vk_framebuffer_create(RendVkTechnique* technique, const RendSwapchainIdx swapchainIdx) {
  RendVkImage* swapchainImage = rend_vk_swapchain_image(technique->swapchain, swapchainIdx);

  VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = technique->vkRenderPass,
      .attachmentCount = 1,
      .pAttachments    = &swapchainImage->vkImageView,
      .width           = swapchainImage->size.x,
      .height          = swapchainImage->size.y,
      .layers          = 1,
  };
  VkFramebuffer result;
  rend_vk_call(
      vkCreateFramebuffer,
      technique->device->vkDevice,
      &framebufferInfo,
      technique->device->vkAllocHost,
      &result);
  return result;
}

static void rend_vk_resource_init(RendVkTechnique* technique) {

  dynarray_for_t(&technique->frameBuffers, VkFramebuffer, fb, {
    vkDestroyFramebuffer(technique->device->vkDevice, *fb, technique->device->vkAllocHost);
  });
  dynarray_clear(&technique->frameBuffers);

  for (u32 i = 0; i != rend_vk_swapchain_imagecount(technique->swapchain); ++i) {
    *dynarray_push_t(&technique->frameBuffers, VkFramebuffer) =
        rend_vk_framebuffer_create(technique, i);
  }

  technique->swapchainVersion = rend_vk_swapchain_version(technique->swapchain);
}

RendVkTechnique* rend_vk_technique_create(RendVkDevice* dev, RendVkSwapchain* swapchain) {
  RendVkTechnique* technique = alloc_alloc_t(g_alloc_heap, RendVkTechnique);
  *technique                 = (RendVkTechnique){
      .device           = dev,
      .swapchain        = swapchain,
      .frameBuffers     = dynarray_create_t(g_alloc_heap, VkFramebuffer, 2),
      .vkRenderPass     = rend_vk_renderpass_create(dev, swapchain),
      .swapchainVersion = u64_max,
  };
  return technique;
}

void rend_vk_technique_destroy(RendVkTechnique* technique) {
  vkDestroyRenderPass(
      technique->device->vkDevice, technique->vkRenderPass, technique->device->vkAllocHost);

  dynarray_for_t(&technique->frameBuffers, VkFramebuffer, fb, {
    vkDestroyFramebuffer(technique->device->vkDevice, *fb, technique->device->vkAllocHost);
  });
  dynarray_destroy(&technique->frameBuffers);

  alloc_free_t(g_alloc_heap, technique);
}

void rend_vk_technique_begin(
    RendVkTechnique*       technique,
    VkCommandBuffer        vkCommandBuffer,
    const RendSwapchainIdx swapchainIdx,
    const RendColor        clearColor) {

  if (technique->swapchainVersion != rend_vk_swapchain_version(technique->swapchain)) {
    rend_vk_resource_init(technique);
  }

  RendVkImage* swapchainImage = rend_vk_swapchain_image(technique->swapchain, swapchainIdx);

  VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  VkRenderPassBeginInfo renderPassInfo = {
      .sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass        = technique->vkRenderPass,
      .framebuffer       = *dynarray_at_t(&technique->frameBuffers, swapchainIdx, VkFramebuffer),
      .renderArea.offset = {0, 0},
      .renderArea.extent.width  = (u32)swapchainImage->size.x,
      .renderArea.extent.height = (u32)swapchainImage->size.y,
      .clearValueCount          = array_elems(clearValues),
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(vkCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void rend_vk_technique_end(RendVkTechnique* technique, VkCommandBuffer vkCommandBuffer) {
  (void)technique;
  vkCmdEndRenderPass(vkCommandBuffer);
}
