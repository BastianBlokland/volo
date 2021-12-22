#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

#include "device_internal.h"
#include "image_internal.h"
#include "technique_internal.h"

#include <vulkan/vulkan_core.h>

#define attachment_max 8

static const VkFormat g_colorAttachmentFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct sRvkTechnique {
  RvkDevice*    dev;
  VkRenderPass  vkRendPass;
  RvkImage      colorAttachment;
  VkFramebuffer vkFrameBuffer;
};

static void rvk_memory_barrier(
    VkCommandBuffer            buffer,
    const VkAccessFlags        srcAccess,
    const VkAccessFlags        dstAccess,
    const VkPipelineStageFlags srcStageFlags,
    const VkPipelineStageFlags dstStageFlags) {

  const VkMemoryBarrier barrier = {
      .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = srcAccess,
      .dstAccessMask = dstAccess,
  };
  vkCmdPipelineBarrier(buffer, srcStageFlags, dstStageFlags, 0, 1, &barrier, 0, null, 0, null);
}

static VkRenderPass rvk_renderpass_create(RvkDevice* dev) {
  VkAttachmentDescription attachments[attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[attachment_max];
  u32                     colorRefCount = 0;

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = g_colorAttachmentFormat,
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
  VkRenderPassCreateInfo renderPassInfo = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachmentCount,
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = 0,
      .pDependencies   = null,
  };
  VkRenderPass result;
  rvk_call(vkCreateRenderPass, dev->vkDev, &renderPassInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFramebuffer rvk_framebuffer_create(RvkTechnique* tech, RvkImage* colorAttachment) {
  VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = tech->vkRendPass,
      .attachmentCount = 1,
      .pAttachments    = &colorAttachment->vkImageView,
      .width           = colorAttachment->size.width,
      .height          = colorAttachment->size.height,
      .layers          = 1,
  };
  VkFramebuffer result;
  rvk_call(vkCreateFramebuffer, tech->dev->vkDev, &framebufferInfo, &tech->dev->vkAlloc, &result);
  return result;
}

static void rvk_technique_resource_create(RvkTechnique* tech, const RendSize size) {
  tech->colorAttachment =
      rvk_image_create_attach_color(tech->dev, g_colorAttachmentFormat, size, RvkImageFlags_None);
  tech->vkFrameBuffer = rvk_framebuffer_create(tech, &tech->colorAttachment);
}

static void rvk_technique_resource_destroy(RvkTechnique* tech) {
  if (!tech->colorAttachment.size.width || !tech->colorAttachment.size.height) {
    return;
  }
  rvk_image_destroy(&tech->colorAttachment, tech->dev);
  vkDestroyFramebuffer(tech->dev->vkDev, tech->vkFrameBuffer, &tech->dev->vkAlloc);
}

RvkTechnique* rvk_technique_create(RvkDevice* dev) {
  RvkTechnique* technique = alloc_alloc_t(g_alloc_heap, RvkTechnique);
  *technique              = (RvkTechnique){
      .dev        = dev,
      .vkRendPass = rvk_renderpass_create(dev),
  };
  return technique;
}

void rvk_technique_destroy(RvkTechnique* tech) {

  rvk_technique_resource_destroy(tech);
  vkDestroyRenderPass(tech->dev->vkDev, tech->vkRendPass, &tech->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, tech);
}

VkRenderPass rvk_technique_vkrendpass(const RvkTechnique* tech) { return tech->vkRendPass; }

RvkImage* rvk_technique_output(RvkTechnique* tech) { return &tech->colorAttachment; }

void rvk_technique_output_barrier(RvkTechnique* tech, VkCommandBuffer vkCmdBuf) {
  (void)tech;
  rvk_memory_barrier(
      vkCmdBuf,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void rvk_technique_begin(
    RvkTechnique* tech, VkCommandBuffer vkCmdBuf, const RendSize size, const RendColor clearColor) {

  if (!rend_size_equal(tech->colorAttachment.size, size)) {
    rvk_technique_resource_destroy(tech);
    rvk_technique_resource_create(tech, size);
  }

  VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  VkRenderPassBeginInfo renderPassInfo = {
      .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass               = tech->vkRendPass,
      .framebuffer              = tech->vkFrameBuffer,
      .renderArea.offset        = {0, 0},
      .renderArea.extent.width  = size.width,
      .renderArea.extent.height = size.height,
      .clearValueCount          = array_elems(clearValues),
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(vkCmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  rvk_image_transition_external(&tech->colorAttachment, RvkImagePhase_ColorAttachment);
}

void rvk_technique_end(RvkTechnique* tech, VkCommandBuffer vkCmdBuf) {

  vkCmdEndRenderPass(vkCmdBuf);
  rvk_image_transition_external(&tech->colorAttachment, RvkImagePhase_TransferSource);
}
