#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "log_logger.h"

#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "mesh_internal.h"
#include "pass_internal.h"
#include "uniform_internal.h"

#define pass_instance_count_max 2048
#define pass_attachment_max 8

typedef RvkGraphic* RvkGraphicPtr;

static const VkFormat g_attachColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

typedef enum {
  RvkPassPrivateFlags_Setup           = 1 << (RvkPassFlags_Count + 0),
  RvkPassPrivateFlags_Active          = 1 << (RvkPassFlags_Count + 1),
  RvkPassPrivateFlags_BoundGlobalData = 1 << (RvkPassFlags_Count + 2),
} RvkPassPrivateFlags;

struct sRvkPass {
  RvkDevice*       dev;
  RvkStatRecorder* statrecorder;
  RvkPassFlags     flags;
  RvkSize          size;
  VkRenderPass     vkRendPass;
  VkPipelineLayout vkGlobalLayout;
  RvkImage         attachColor, attachDepth;
  VkFramebuffer    vkFrameBuffer;
  VkCommandBuffer  vkCmdBuf;
  RvkUniformPool*  uniformPool;
  DynArray         dynDescSets; // RvkDescSet[]
};

static VkRenderPass rvk_renderpass_create(RvkDevice* dev, const RvkPassFlags flags) {
  VkAttachmentDescription attachments[pass_attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[pass_attachment_max];
  u32                     colorRefCount = 0;

  attachments[attachmentCount++] = (VkAttachmentDescription){
      .format         = g_attachColorFormat,
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = (flags & RvkPassFlags_ClearColor) ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                          : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
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
      .loadOp         = (flags & RvkPassFlags_ClearDepth) ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                          : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
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
  const VkSubpassDependency dependencies[] = {
      // Synchronize with the previous run of this renderpass.
      {
          .srcSubpass   = VK_SUBPASS_EXTERNAL,
          .dstSubpass   = 0,
          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          .srcAccessMask = 0,
          .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          .dstAccessMask =
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      },
  };
  const VkRenderPassCreateInfo renderPassInfo = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachmentCount,
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = array_elems(dependencies),
      .pDependencies   = dependencies,
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

static void rvk_pass_viewport_set(VkCommandBuffer vkCmdBuf, const RvkSize size) {
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

static void rvk_pass_scissor_set(VkCommandBuffer vkCmdBuf, const RvkSize size) {
  const VkRect2D scissor = {
      .offset        = {0, 0},
      .extent.width  = size.width,
      .extent.height = size.height,
  };
  vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
}

static void rvk_pass_vkrenderpass_begin(
    RvkPass* pass, VkCommandBuffer vkCmdBuf, const RvkSize size, const GeoColor clearColor) {

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

static void rvk_pass_resource_create(RvkPass* pass, const RvkSize size) {
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

static void rvk_pass_free_dyn_desc(RvkPass* pass) {
  dynarray_for_t(&pass->dynDescSets, RvkDescSet, set) { rvk_desc_free(*set); }
  dynarray_clear(&pass->dynDescSets);
}

static RvkDescSet rvk_pass_alloc_dyn_desc(RvkPass* pass, const RvkDescMeta* meta) {
  const RvkDescSet res                             = rvk_desc_alloc(pass->dev->descPool, meta);
  *dynarray_push_t(&pass->dynDescSets, RvkDescSet) = res;
  return res;
}

static void rvk_pass_bind_dyn_mesh(RvkPass* pass, RvkGraphic* graphic, RvkMesh* mesh) {
  const RvkDescMeta meta    = {.bindings[0] = RvkDescKind_StorageBuffer};
  const RvkDescSet  descSet = rvk_pass_alloc_dyn_desc(pass, &meta);
  rvk_desc_set_attach_buffer(graphic->descSet, 0, &mesh->vertexBuffer, 0);

  VkDescriptorSet vkDescSet = rvk_desc_set_vkset(descSet);
  vkCmdBindDescriptorSets(
      pass->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      graphic->vkPipelineLayout,
      RvkGraphicSet_Dynamic,
      1,
      &vkDescSet,
      0,
      null);
}

RvkPass* rvk_pass_create(
    RvkDevice*         dev,
    VkCommandBuffer    vkCmdBuf,
    RvkUniformPool*    uniformPool,
    const RvkPassFlags flags) {

  RvkPass* pass = alloc_alloc_t(g_alloc_heap, RvkPass);
  *pass         = (RvkPass){
      .dev            = dev,
      .statrecorder   = rvk_statrecorder_create(dev),
      .vkRendPass     = rvk_renderpass_create(dev, flags),
      .vkGlobalLayout = rvk_global_layout_create(dev, rvk_uniform_vkdesclayout(uniformPool)),
      .vkCmdBuf       = vkCmdBuf,
      .uniformPool    = uniformPool,
      .flags          = flags,
      .dynDescSets    = dynarray_create_t(g_alloc_heap, RvkDescSet, 64),
  };
  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {
  rvk_pass_free_dyn_desc(pass);

  rvk_statrecorder_destroy(pass->statrecorder);
  rvk_pass_resource_destroy(pass);
  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);
  vkDestroyPipelineLayout(pass->dev->vkDev, pass->vkGlobalLayout, &pass->dev->vkAlloc);
  dynarray_destroy(&pass->dynDescSets);

  alloc_free_t(g_alloc_heap, pass);
}

bool rvk_pass_active(const RvkPass* pass) {
  return (pass->flags & RvkPassPrivateFlags_Active) != 0;
}

RvkImage* rvk_pass_output(RvkPass* pass) { return &pass->attachColor; }

u64 rvk_pass_stat(RvkPass* pass, const RvkStat stat) {
  return rvk_statrecorder_query(pass->statrecorder, stat);
}

void rvk_pass_setup(RvkPass* pass, const RvkSize size) {
  diag_assert_msg(size.width && size.height, "Pass cannot be zero sized");

  rvk_statrecorder_reset(pass->statrecorder, pass->vkCmdBuf);
  rvk_pass_free_dyn_desc(pass); // Free last frame's dynamic descriptors.

  if (rvk_size_equal(pass->size, size)) {
    return;
  }
  pass->flags |= RvkPassPrivateFlags_Setup;
  rvk_pass_resource_destroy(pass);
  rvk_pass_resource_create(pass, size);
}

bool rvk_pass_prepare(RvkPass* pass, RvkGraphic* graphic) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");
  return rvk_graphic_prepare(graphic, pass->vkCmdBuf, pass->vkRendPass);
}

void rvk_pass_begin(RvkPass* pass, const GeoColor clearColor) {
  diag_assert_msg(pass->flags & RvkPassPrivateFlags_Setup, "Pass not setup");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  pass->flags |= RvkPassPrivateFlags_Active;

  rvk_statrecorder_start(pass->statrecorder, pass->vkCmdBuf);
  rvk_debug_label_begin(pass->dev->debug, pass->vkCmdBuf, geo_color_blue, "pass");

  rvk_pass_vkrenderpass_begin(pass, pass->vkCmdBuf, pass->attachColor.size, clearColor);
  rvk_image_transition_external(&pass->attachColor, RvkImagePhase_ColorAttachment);
  rvk_image_transition_external(&pass->attachDepth, RvkImagePhase_DepthAttachment);

  rvk_pass_viewport_set(pass->vkCmdBuf, pass->attachColor.size);
  rvk_pass_scissor_set(pass->vkCmdBuf, pass->attachColor.size);
}

static u32 rvk_pass_instances_per_draw(RvkPass* pass, const u32 remaining, const u32 dataStride) {
  if (!dataStride) {
    return math_min(remaining, pass_instance_count_max);
  }
  const u32 instances = math_min(remaining, rvk_uniform_size_max(pass->uniformPool) / dataStride);
  return math_min(instances, pass_instance_count_max);
}

static void rvk_pass_draw_submit(RvkPass* pass, const RvkPassDraw* draw) {
  const bool  hasGlobalData = (pass->flags & RvkPassPrivateFlags_BoundGlobalData) != 0;
  RvkGraphic* graphic       = draw->graphic;

  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireGlobalData && !hasGlobalData)) {
    log_w("Graphic requires global data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireDynamicMesh && !draw->dynMesh)) {
    log_w("Graphic requires a dynamic mesh", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireDrawData && !draw->drawData.size)) {
    log_w("Graphic requires draw data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireInstanceData && !draw->instDataStride)) {
    log_w("Graphic requires instance data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(draw->instDataStride > rvk_uniform_size_max(pass->uniformPool))) {
    log_w(
        "Draw instance data exceeds maximum",
        log_param("graphic", fmt_text(graphic->dbgName)),
        log_param("size", fmt_size(draw->instDataStride)),
        log_param("size-max", fmt_size(rvk_uniform_size_max(pass->uniformPool))));
    return;
  }

  rvk_statrecorder_report(pass->statrecorder, RvkStat_Draws, 1);
  rvk_debug_label_begin(
      pass->dev->debug, pass->vkCmdBuf, geo_color_green, "draw_{}", fmt_text(graphic->dbgName));

  rvk_graphic_bind(graphic, pass->vkCmdBuf);

  if (draw->dynMesh) {
    rvk_pass_bind_dyn_mesh(pass, graphic, draw->dynMesh);
  }
  if (draw->drawData.size) {
    rvk_uniform_bind(
        pass->uniformPool,
        draw->drawData,
        pass->vkCmdBuf,
        graphic->vkPipelineLayout,
        RvkGraphicSet_Draw);
  }

  diag_assert(draw->instDataStride * draw->instCount == draw->instData.size);
  const u32 dataStride =
      graphic->flags & RvkGraphicFlags_RequireInstanceData ? draw->instDataStride : 0;

  for (u32 remInstCount = draw->instCount, dataOffset = 0; remInstCount != 0;) {
    const u32 instCount = rvk_pass_instances_per_draw(pass, remInstCount, dataStride);
    rvk_statrecorder_report(pass->statrecorder, RvkStat_Instances, instCount);

    if (dataStride) {
      const u32 dataSize = instCount * dataStride;
      rvk_uniform_bind(
          pass->uniformPool,
          mem_slice(draw->instData, dataOffset, dataSize),
          pass->vkCmdBuf,
          graphic->vkPipelineLayout,
          RvkGraphicSet_Instance);
      dataOffset += dataSize;
    }

    if (graphic->mesh) {
      vkCmdDrawIndexed(pass->vkCmdBuf, graphic->mesh->indexCount, instCount, 0, 0, 0);
    } else {
      const u32 vertexCount =
          draw->vertexCountOverride ? draw->vertexCountOverride : graphic->vertexCount;
      if (LIKELY(vertexCount)) {
        vkCmdDraw(pass->vkCmdBuf, vertexCount, instCount, 0, 0);
      }
    }
    remInstCount -= instCount;
  }

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
}

void rvk_pass_draw(RvkPass* pass, Mem globalData, const RvkPassDrawList drawList) {
  diag_assert_msg(pass->flags & RvkPassPrivateFlags_Active, "Pass not active");

  if (globalData.size) {
    rvk_uniform_bind(pass->uniformPool, globalData, pass->vkCmdBuf, pass->vkGlobalLayout, 0);
    pass->flags |= RvkPassPrivateFlags_BoundGlobalData;
  }

  array_ptr_for_t(drawList, RvkPassDraw, draw) { rvk_pass_draw_submit(pass, draw); }

  pass->flags &= ~RvkPassPrivateFlags_BoundGlobalData;
}

void rvk_pass_end(RvkPass* pass) {
  diag_assert_msg(pass->flags & RvkPassPrivateFlags_Active, "Pass not active");
  pass->flags &= ~RvkPassPrivateFlags_Active;

  rvk_statrecorder_stop(pass->statrecorder, pass->vkCmdBuf);
  vkCmdEndRenderPass(pass->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
}
