#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

#include "device_internal.h"
#include "technique_internal.h"

#define attachment_max 8

struct sRvkTechnique {
  RvkDevice*    dev;
  RvkSwapchain* swapchain;
  VkRenderPass  vkRendPass;
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
  rvk_call(vkCreateRenderPass, dev->vkDev, &renderPassInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFramebuffer
rvk_framebuffer_create(RvkTechnique* tech, const RvkSwapchainIdx swapchainIdx) {
  RvkImage* swapchainImage = rvk_swapchain_image(tech->swapchain, swapchainIdx);

  VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = tech->vkRendPass,
      .attachmentCount = 1,
      .pAttachments    = &swapchainImage->vkImageView,
      .width           = swapchainImage->size.width,
      .height          = swapchainImage->size.height,
      .layers          = 1,
  };
  VkFramebuffer result;
  rvk_call(vkCreateFramebuffer, tech->dev->vkDev, &framebufferInfo, &tech->dev->vkAlloc, &result);
  return result;
}

static void rvk_resource_init(RvkTechnique* tech) {

  dynarray_for_t(&tech->frameBuffers, VkFramebuffer, fb) {
    vkDestroyFramebuffer(tech->dev->vkDev, *fb, &tech->dev->vkAlloc);
  }
  dynarray_clear(&tech->frameBuffers);

  for (u32 i = 0; i != rvk_swapchain_imagecount(tech->swapchain); ++i) {
    *dynarray_push_t(&tech->frameBuffers, VkFramebuffer) = rvk_framebuffer_create(tech, i);
  }

  tech->swapchainVersion = rvk_swapchain_version(tech->swapchain);
}

RvkTechnique* rvk_technique_create(RvkDevice* dev, RvkSwapchain* swapchain) {
  RvkTechnique* technique = alloc_alloc_t(g_alloc_heap, RvkTechnique);
  *technique              = (RvkTechnique){
      .dev              = dev,
      .swapchain        = swapchain,
      .frameBuffers     = dynarray_create_t(g_alloc_heap, VkFramebuffer, 2),
      .vkRendPass       = rvk_renderpass_create(dev, swapchain),
      .swapchainVersion = u64_max,
  };
  return technique;
}

void rvk_technique_destroy(RvkTechnique* tech) {
  vkDestroyRenderPass(tech->dev->vkDev, tech->vkRendPass, &tech->dev->vkAlloc);

  dynarray_for_t(&tech->frameBuffers, VkFramebuffer, fb) {
    vkDestroyFramebuffer(tech->dev->vkDev, *fb, &tech->dev->vkAlloc);
  }
  dynarray_destroy(&tech->frameBuffers);

  alloc_free_t(g_alloc_heap, tech);
}

VkRenderPass rvk_technique_vkrendpass(RvkTechnique* tech) { return tech->vkRendPass; }

void rvk_technique_begin(
    RvkTechnique*         tech,
    VkCommandBuffer       vkCmdBuf,
    const RvkSwapchainIdx swapchainIdx,
    const RendColor       clearColor) {

  if (tech->swapchainVersion != rvk_swapchain_version(tech->swapchain)) {
    rvk_resource_init(tech);
  }

  RvkImage* swapchainImage = rvk_swapchain_image(tech->swapchain, swapchainIdx);

  VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  VkRenderPassBeginInfo renderPassInfo = {
      .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass               = tech->vkRendPass,
      .framebuffer              = *dynarray_at_t(&tech->frameBuffers, swapchainIdx, VkFramebuffer),
      .renderArea.offset        = {0, 0},
      .renderArea.extent.width  = swapchainImage->size.width,
      .renderArea.extent.height = swapchainImage->size.height,
      .clearValueCount          = array_elems(clearValues),
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(vkCmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  rvk_image_transition_external(swapchainImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void rvk_technique_end(
    RvkTechnique* tech, VkCommandBuffer vkCmdBuf, const RvkSwapchainIdx swapchainIdx) {

  vkCmdEndRenderPass(vkCmdBuf);

  RvkImage* swapchainImage = rvk_swapchain_image(tech->swapchain, swapchainIdx);
  rvk_image_transition_external(swapchainImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
