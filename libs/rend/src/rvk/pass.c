#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "log_logger.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "pass_internal.h"
#include "uniform_internal.h"

#define attachment_max 8

typedef RvkGraphic* RvkGraphicPtr;

static const VkFormat g_attachColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

typedef enum {
  RvkPassFlags_Setup  = 1 << 0,
  RvkPassFlags_Active = 1 << 1,
} RvkPassFlags;

struct sRvkPass {
  RvkDevice*       dev;
  RvkPassFlags     flags;
  RendSize         size;
  VkRenderPass     vkRendPass;
  VkPipelineLayout vkGlobalLayout;
  RvkImage         attachColor, attachDepth;
  VkFramebuffer    vkFrameBuffer;
  VkCommandBuffer  vkCmdBuf;
  RvkUniformPool*  uniformPool;
};

static VkRenderPass rvk_renderpass_create(RvkDevice* dev) {
  VkAttachmentDescription attachments[attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[attachment_max];
  u32                     colorRefCount = 0;

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = g_attachColorFormat,
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  colorRefs[colorRefCount++] = (VkAttachmentReference){
      .attachment = attachmentCount - 1,
      .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = dev->vkDepthFormat,
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  const VkAttachmentReference depthAttachmentRef = {
      .attachment = attachmentCount - 1,
      .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  const VkSubpassDescription subpass = {
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount    = colorRefCount,
      .pColorAttachments       = colorRefs,
      .pDepthStencilAttachment = &depthAttachmentRef,
  };
  const VkRenderPassCreateInfo renderPassInfo = {
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

/**
 * Create a pipeline layout with a single global descriptor-set 0.
 * All pipeline layouts have to be compatible with this layout.
 * This allows us to share the global data binding between different pipelines.
 */
static VkPipelineLayout
rvk_global_layout_create(RvkDevice* dev, VkDescriptorSetLayout vkGlobalDescLayout) {

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts    = &vkGlobalDescLayout,
  };
  VkPipelineLayout result;
  rvk_call(vkCreatePipelineLayout, dev->vkDev, &pipelineLayoutInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFramebuffer
rvk_framebuffer_create(RvkPass* pass, RvkImage* attachColor, RvkImage* attachDepth) {
  const VkImageView attachments[] = {
      attachColor->vkImageView,
      attachDepth->vkImageView,
  };
  const VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = pass->vkRendPass,
      .attachmentCount = array_elems(attachments),
      .pAttachments    = attachments,
      .width           = pass->size.width,
      .height          = pass->size.height,
      .layers          = 1,
  };
  VkFramebuffer result;
  rvk_call(vkCreateFramebuffer, pass->dev->vkDev, &framebufferInfo, &pass->dev->vkAlloc, &result);
  return result;
}

static void rvk_pass_viewport_set(VkCommandBuffer vkCmdBuf, const RendSize size) {
  const VkViewport viewport = {
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
  const VkRect2D scissor = {
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
      {.depthStencil = {.depth = 0.0f}}, // Init depth to zero for a reversed-z depthbuffer.
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
  pass->size          = size;
  pass->attachColor   = rvk_image_create_attach_color(pass->dev, g_attachColorFormat, size);
  pass->attachDepth   = rvk_image_create_attach_depth(pass->dev, pass->dev->vkDepthFormat, size);
  pass->vkFrameBuffer = rvk_framebuffer_create(pass, &pass->attachColor, &pass->attachDepth);
}

static void rvk_pass_resource_destroy(RvkPass* pass) {
  if (!pass->size.width || !pass->size.height) {
    return;
  }
  rvk_image_destroy(&pass->attachColor, pass->dev);
  rvk_image_destroy(&pass->attachDepth, pass->dev);
  vkDestroyFramebuffer(pass->dev->vkDev, pass->vkFrameBuffer, &pass->dev->vkAlloc);
}

RvkPass* rvk_pass_create(RvkDevice* dev, VkCommandBuffer vkCmdBuf, RvkUniformPool* uniformPool) {
  RvkPass* pass = alloc_alloc_t(g_alloc_heap, RvkPass);
  *pass         = (RvkPass){
      .dev            = dev,
      .vkRendPass     = rvk_renderpass_create(dev),
      .vkGlobalLayout = rvk_global_layout_create(dev, rvk_uniform_vkdesclayout(uniformPool)),
      .vkCmdBuf       = vkCmdBuf,
      .uniformPool    = uniformPool,
  };
  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {

  rvk_pass_resource_destroy(pass);
  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);
  vkDestroyPipelineLayout(pass->dev->vkDev, pass->vkGlobalLayout, &pass->dev->vkAlloc);

  alloc_free_t(g_alloc_heap, pass);
}

bool rvk_pass_active(const RvkPass* pass) { return (pass->flags & RvkPassFlags_Active) != 0; }

RvkImage* rvk_pass_output(RvkPass* pass) { return &pass->attachColor; }

void rvk_pass_setup(RvkPass* pass, const RendSize size) {
  diag_assert_msg(size.width && size.height, "Pass cannot be zero sized");

  if (rend_size_equal(pass->size, size)) {
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

  rvk_debug_label_begin(pass->dev->debug, pass->vkCmdBuf, rend_blue, "pass");

  rvk_pass_vkrenderpass_begin(pass, pass->vkCmdBuf, pass->attachColor.size, clearColor);
  rvk_image_transition_external(&pass->attachColor, RvkImagePhase_ColorAttachment);

  rvk_pass_viewport_set(pass->vkCmdBuf, pass->attachColor.size);
  rvk_pass_scissor_set(pass->vkCmdBuf, pass->attachColor.size);
}

void rvk_pass_draw(RvkPass* pass, Mem uniformData, const RvkPassDrawList drawList) {
  diag_assert_msg(pass->flags & RvkPassFlags_Active, "Pass not active");

  if (uniformData.size) {
    rvk_uniform_bind(pass->uniformPool, uniformData, pass->vkCmdBuf, pass->vkGlobalLayout, 0);
  }

  array_ptr_for_t(drawList, RvkPassDraw, draw) {
    if (draw->graphic->flags & RvkGraphicFlags_UsesGlobalData && !uniformData.size) {
      log_w("Graphic requires global data", log_param("graphic", fmt_text(draw->graphic->dbgName)));
      continue;
    }
    rvk_debug_label_begin(
        pass->dev->debug, pass->vkCmdBuf, rend_green, "draw_{}", fmt_text(draw->graphic->dbgName));

    rvk_graphic_bind(draw->graphic, pass->vkCmdBuf);

    const u32 indexCount = rvk_graphic_index_count(draw->graphic);
    vkCmdDrawIndexed(pass->vkCmdBuf, indexCount, 1, 0, 0, 0);

    rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
  }
}

void rvk_pass_end(RvkPass* pass) {
  diag_assert_msg(pass->flags & RvkPassFlags_Active, "Pass not active");
  pass->flags &= ~RvkPassFlags_Active;

  vkCmdEndRenderPass(pass->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
}
