#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"

#include "device_internal.h"
#include "image_internal.h"
#include "pass_internal.h"

#define attachment_max 8

static const VkFormat g_colorAttachmentFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct sRvkPass {
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

static VkFramebuffer rvk_framebuffer_create(RvkPass* pass, RvkImage* colorAttachment) {
  VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = pass->vkRendPass,
      .attachmentCount = 1,
      .pAttachments    = &colorAttachment->vkImageView,
      .width           = colorAttachment->size.width,
      .height          = colorAttachment->size.height,
      .layers          = 1,
  };
  VkFramebuffer result;
  rvk_call(vkCreateFramebuffer, pass->dev->vkDev, &framebufferInfo, &pass->dev->vkAlloc, &result);
  return result;
}

static void rvk_pass_resource_create(RvkPass* pass, const RendSize size) {
  pass->colorAttachment =
      rvk_image_create_attach_color(pass->dev, g_colorAttachmentFormat, size, RvkImageFlags_None);
  pass->vkFrameBuffer = rvk_framebuffer_create(pass, &pass->colorAttachment);
}

static void rvk_pass_resource_destroy(RvkPass* pass) {
  if (!pass->colorAttachment.size.width || !pass->colorAttachment.size.height) {
    return;
  }
  rvk_image_destroy(&pass->colorAttachment, pass->dev);
  vkDestroyFramebuffer(pass->dev->vkDev, pass->vkFrameBuffer, &pass->dev->vkAlloc);
}

RvkPass* rvk_pass_create(RvkDevice* dev) {
  RvkPass* pass = alloc_alloc_t(g_alloc_heap, RvkPass);
  *pass         = (RvkPass){
      .dev        = dev,
      .vkRendPass = rvk_renderpass_create(dev),
  };
  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {

  rvk_pass_resource_destroy(pass);
  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, pass);
}

VkRenderPass rvk_pass_vkrendpass(const RvkPass* pass) { return pass->vkRendPass; }

RvkImage* rvk_pass_output(RvkPass* pass) { return &pass->colorAttachment; }

void rvk_pass_output_barrier(RvkPass* pass, VkCommandBuffer vkCmdBuf) {
  (void)pass;
  rvk_memory_barrier(
      vkCmdBuf,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void rvk_pass_begin(
    RvkPass* pass, VkCommandBuffer vkCmdBuf, const RendSize size, const RendColor clearColor) {

  if (!rend_size_equal(pass->colorAttachment.size, size)) {
    rvk_pass_resource_destroy(pass);
    rvk_pass_resource_create(pass, size);
  }

  VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  VkRenderPassBeginInfo renderPassInfo = {
      .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass               = pass->vkRendPass,
      .framebuffer              = pass->vkFrameBuffer,
      .renderArea.offset        = {0, 0},
      .renderArea.extent.width  = size.width,
      .renderArea.extent.height = size.height,
      .clearValueCount          = array_elems(clearValues),
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(vkCmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  rvk_image_transition_external(&pass->colorAttachment, RvkImagePhase_ColorAttachment);
}

void rvk_pass_end(RvkPass* pass, VkCommandBuffer vkCmdBuf) {

  vkCmdEndRenderPass(vkCmdBuf);
  rvk_image_transition_external(&pass->colorAttachment, RvkImagePhase_TransferSource);
}
