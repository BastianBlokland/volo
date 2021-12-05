#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

#include "technique_internal.h"

#define attachment_max 8

struct sRvkTechnique {
  RvkDevice*    device;
  RvkSwapchain* swapchain;
  VkRenderPass  vkRenderPass;
  u64           swapchainVersion;
  DynArray      frameBuffers; // VkFramebuffer[]
};

static VkRenderPass rvk_renderpass_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  VkAttachmentDescription attachments[attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[attachment_max];
  u32                     colorRefCount = 0;

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = rvk_swapchain_format(swapchain),
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
  rvk_call(vkCreateRenderPass, dev->vkDevice, &renderPassInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFramebuffer
rvk_framebuffer_create(RvkTechnique* technique, const RvkSwapchainIdx swapchainIdx) {
  RvkImage* swapchainImage = rvk_swapchain_image(technique->swapchain, swapchainIdx);

  VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = technique->vkRenderPass,
      .attachmentCount = 1,
      .pAttachments    = &swapchainImage->vkImageView,
      .width           = swapchainImage->size.width,
      .height          = swapchainImage->size.height,
      .layers          = 1,
  };
  VkFramebuffer result;
  rvk_call(
      vkCreateFramebuffer,
      technique->device->vkDevice,
      &framebufferInfo,
      &technique->device->vkAlloc,
      &result);
  return result;
}

static void rvk_resource_init(RvkTechnique* technique) {

  dynarray_for_t(&technique->frameBuffers, VkFramebuffer, fb, {
    vkDestroyFramebuffer(technique->device->vkDevice, *fb, &technique->device->vkAlloc);
  });
  dynarray_clear(&technique->frameBuffers);

  for (u32 i = 0; i != rvk_swapchain_imagecount(technique->swapchain); ++i) {
    *dynarray_push_t(&technique->frameBuffers, VkFramebuffer) =
        rvk_framebuffer_create(technique, i);
  }

  technique->swapchainVersion = rvk_swapchain_version(technique->swapchain);
}

RvkTechnique* rvk_technique_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  RvkTechnique* technique = alloc_alloc_t(g_alloc_heap, RvkTechnique);
  *technique              = (RvkTechnique){
      .device           = dev,
      .swapchain        = swapchain,
      .frameBuffers     = dynarray_create_t(g_alloc_heap, VkFramebuffer, 2),
      .vkRenderPass     = rvk_renderpass_create(dev, swapchain),
      .swapchainVersion = u64_max,
  };
  return technique;
}

void rvk_technique_destroy(RvkTechnique* technique) {
  vkDestroyRenderPass(
      technique->device->vkDevice, technique->vkRenderPass, &technique->device->vkAlloc);

  dynarray_for_t(&technique->frameBuffers, VkFramebuffer, fb, {
    vkDestroyFramebuffer(technique->device->vkDevice, *fb, &technique->device->vkAlloc);
  });
  dynarray_destroy(&technique->frameBuffers);

  alloc_free_t(g_alloc_heap, technique);
}

void rvk_technique_begin(
    RvkTechnique*         technique,
    VkCommandBuffer       vkCommandBuffer,
    const RvkSwapchainIdx swapchainIdx,
    const RendColor       clearColor) {

  if (technique->swapchainVersion != rvk_swapchain_version(technique->swapchain)) {
    rvk_resource_init(technique);
  }

  RvkImage* swapchainImage = rvk_swapchain_image(technique->swapchain, swapchainIdx);

  VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  VkRenderPassBeginInfo renderPassInfo = {
      .sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass        = technique->vkRenderPass,
      .framebuffer       = *dynarray_at_t(&technique->frameBuffers, swapchainIdx, VkFramebuffer),
      .renderArea.offset = {0, 0},
      .renderArea.extent.width  = swapchainImage->size.width,
      .renderArea.extent.height = swapchainImage->size.height,
      .clearValueCount          = array_elems(clearValues),
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(vkCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void rvk_technique_end(RvkTechnique* technique, VkCommandBuffer vkCommandBuffer) {
  (void)technique;
  vkCmdEndRenderPass(vkCommandBuffer);
}
