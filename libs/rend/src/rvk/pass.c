#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "pass_internal.h"

#define attachment_max 8

typedef RvkGraphic* RvkGraphicPtr;

static const VkFormat g_colorAttachmentFormat = VK_FORMAT_R8G8B8A8_UNORM;

typedef enum {
  RvkPassFlags_Setup  = 1 << 0,
  RvkPassFlags_Active = 1 << 1,
} RvkPassFlags;

struct sRvkPass {
  RvkDevice*      dev;
  RvkPassFlags    flags;
  VkRenderPass    vkRendPass;
  RvkImage        colorAttachment;
  VkFramebuffer   vkFrameBuffer;
  VkCommandBuffer vkCmdBuf;
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

static void rvk_pass_viewport_set(VkCommandBuffer vkCmdBuf, const RendSize size) {
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

static void rvk_pass_scissor_set(VkCommandBuffer vkCmdBuf, const RendSize size) {
  VkRect2D scissor = {
      .offset        = {0, 0},
      .extent.width  = size.width,
      .extent.height = size.height,
  };
  vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
}

static void rvk_pass_vkrenderpass_begin(
    RvkPass* pass, VkCommandBuffer vkCmdBuf, const RendSize size, const RendColor clearColor) {

  const VkClearValue clearValues[] = {
      *(VkClearColorValue*)&clearColor,
  };
  const VkRenderPassBeginInfo renderPassInfo = {
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

RvkPass* rvk_pass_create(RvkDevice* dev, VkCommandBuffer vkCmdBuf) {
  RvkPass* pass = alloc_alloc_t(g_alloc_heap, RvkPass);
  *pass         = (RvkPass){
      .dev        = dev,
      .vkRendPass = rvk_renderpass_create(dev),
      .vkCmdBuf   = vkCmdBuf,
  };
  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {

  rvk_pass_resource_destroy(pass);
  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, pass);
}

bool rvk_pass_active(const RvkPass* pass) { return (pass->flags & RvkPassFlags_Active) != 0; }

RvkImage* rvk_pass_output(RvkPass* pass) { return &pass->colorAttachment; }

void rvk_pass_output_barrier(RvkPass* pass) {
  rvk_memory_barrier(
      pass->vkCmdBuf,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void rvk_pass_setup(RvkPass* pass, const RendSize size) {
  diag_assert_msg(size.width && size.height, "Pass cannot be zero sized");

  if (rend_size_equal(pass->colorAttachment.size, size)) {
    return;
  }
  pass->flags |= RvkPassFlags_Setup;
  rvk_pass_resource_destroy(pass);
  rvk_pass_resource_create(pass, size);
}

bool rvk_pass_prepare(RvkPass* pass, RvkGraphic* graphic) {
  diag_assert_msg(!(pass->flags & RvkPassFlags_Active), "Pass already active");
  return rvk_graphic_prepare(graphic, pass->vkCmdBuf, pass->vkRendPass);
}

void rvk_pass_begin(RvkPass* pass, const RendColor clearColor) {
  diag_assert_msg(pass->flags & RvkPassFlags_Setup, "Pass not setup");
  diag_assert_msg(!(pass->flags & RvkPassFlags_Active), "Pass already active");

  pass->flags |= RvkPassFlags_Active;

  rvk_pass_vkrenderpass_begin(pass, pass->vkCmdBuf, pass->colorAttachment.size, clearColor);
  rvk_image_transition_external(&pass->colorAttachment, RvkImagePhase_ColorAttachment);

  rvk_pass_viewport_set(pass->vkCmdBuf, pass->colorAttachment.size);
  rvk_pass_scissor_set(pass->vkCmdBuf, pass->colorAttachment.size);
}

void rvk_pass_draw(RvkPass* pass, const RvkPassDrawList drawList) {
  diag_assert_msg(pass->flags & RvkPassFlags_Active, "Pass not active");

  array_ptr_for_t(drawList, RvkPassDraw, draw) {
    rvk_graphic_bind(draw->graphic, pass->vkCmdBuf);

    const u32 indexCount = rvk_graphic_index_count(draw->graphic);
    vkCmdDrawIndexed(pass->vkCmdBuf, indexCount, 1, 0, 0, 0);
  }
}

void rvk_pass_end(RvkPass* pass) {
  diag_assert_msg(pass->flags & RvkPassFlags_Active, "Pass not active");
  pass->flags &= ~RvkPassFlags_Active;

  vkCmdEndRenderPass(pass->vkCmdBuf);
  rvk_image_transition_external(&pass->colorAttachment, RvkImagePhase_TransferSource);
}
