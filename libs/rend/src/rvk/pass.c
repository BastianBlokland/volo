#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "log_logger.h"

#include "attach_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "image_internal.h"
#include "mesh_internal.h"
#include "pass_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "uniform_internal.h"

#define pass_instance_count_max 2048
#define pass_attachment_color_max 2
#define pass_attachment_max (pass_attachment_color_max + 1)
#define pass_dependencies_max 8
#define pass_global_image_max 5

typedef RvkGraphic* RvkGraphicPtr;

typedef enum {
  RvkPassPrivateFlags_Active   = 1 << (RvkPassFlags_Count + 0),
  RvkPassPrivateFlags_Recorded = 1 << (RvkPassFlags_Count + 1),
} RvkPassPrivateFlags;

typedef struct {
  RvkStopwatchRecord timeRecBegin, timeRecEnd;
  VkFramebuffer      vkFrameBuffer;
} RvkPassInvoc;

struct sRvkPass {
  RvkDevice*       dev;
  VkFormat         swapchainFormat;
  RvkStatRecorder* statrecorder;
  RvkStopwatch*    stopwatch;
  RvkPassFlags     flags;
  String           name;
  RvkSize          size;
  VkRenderPass     vkRendPass;
  RvkImage*        attachColors[pass_attachment_color_max];
  RvkImage*        attachDepth;
  VkCommandBuffer  vkCmdBuf;
  RvkUniformPool*  uniformPool;
  VkPipelineLayout globalPipelineLayout;
  RvkDescSet       globalDescSet;
  u32              globalDataOffset;
  u16              globalBoundMask; // Bitset of the bound global resources.
  u16              attachColorMask;
  RvkSampler       globalImageSampler, globalShadowSampler;
  RvkImage*        globalImages[pass_global_image_max];
  DynArray         dynDescSets; // RvkDescSet[]
  DynArray         invocations; // RvkPassInvoc[]
};

static VkFormat rvk_attach_color_format(const bool srgb, const bool flt, const bool single) {
  if (single) {
    return srgb ? VK_FORMAT_R8_SRGB : flt ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_R8_UNORM;
  }
  return srgb  ? VK_FORMAT_R8G8B8A8_SRGB
         : flt ? VK_FORMAT_B10G11R11_UFLOAT_PACK32
               : VK_FORMAT_R8G8B8A8_UNORM;
}

static VkFormat rvk_attach_color_format_at_index(const RvkPass* pass, const u32 index) {
  switch (index) {
  case 0: {
    if (pass->flags & RvkPassFlags_Color1Swapchain) {
      return pass->swapchainFormat;
    }
    const bool srgb   = (pass->flags & RvkPassFlags_Color1Srgb) != 0;
    const bool flt    = (pass->flags & RvkPassFlags_Color1Float) != 0;
    const bool single = (pass->flags & RvkPassFlags_Color1Single) != 0;
    return rvk_attach_color_format(srgb, flt, single);
  }
  case 1: {
    const bool srgb   = (pass->flags & RvkPassFlags_Color2Srgb) != 0;
    const bool flt    = false;
    const bool single = (pass->flags & RvkPassFlags_Color2Single) != 0;
    return rvk_attach_color_format(srgb, flt, single);
  }
  default:
    diag_crash_msg("Unsupported color attachment index: {}", fmt_int(index));
  }
}

static u32 rvk_attach_color_count(const RvkPassFlags flags) {
  u32 result = 0;
  result += (flags & RvkPassFlags_Color1) != 0;
  result += (flags & RvkPassFlags_Color2) != 0;
  return result;
}

#ifndef VOLO_FAST
static void rvk_attach_assert_color(const RvkPass* pass, const u32 idx, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_color(pass, idx);
  if (idx == 0 && pass->flags & RvkPassFlags_Color1Swapchain) {
    diag_assert_msg(
        img->type == RvkImageType_Swapchain,
        "Pass {} color attachment {} invalid: Expected a swapchain image",
        fmt_text(pass->name),
        fmt_int(idx));
  }
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentColor,
      "Pass {} color attachment {} invalid: Missing AttachmentColor capability",
      fmt_text(pass->name),
      fmt_int(idx));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} color attachment {} invalid: Missing capabilities",
      fmt_text(pass->name),
      fmt_int(idx));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} color attachment {} invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->name),
      fmt_int(idx),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
  diag_assert_msg(
      img->size.data == pass->size.data,
      "Pass {} color attachment {} invalid: Invalid size (expected: {}x{}, actual: {}x{})",
      fmt_text(pass->name),
      fmt_int(idx),
      fmt_int(pass->size.width),
      fmt_int(pass->size.height),
      fmt_int(img->size.width),
      fmt_int(img->size.height));
}

static void rvk_attach_assert_depth(const RvkPass* pass, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_depth(pass);
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentDepth,
      "Pass {} depth attachment invalid: Missing AttachmentDepth capability",
      fmt_text(pass->name));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} depth attachment invalid: Missing capabilities",
      fmt_text(pass->name));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} depth attachment invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->name),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
  diag_assert_msg(
      img->size.data == pass->size.data,
      "Pass {} depth attachment invalid: Invalid size (expected: {}x{}, actual: {}x{})",
      fmt_text(pass->name),
      fmt_int(pass->size.width),
      fmt_int(pass->size.height),
      fmt_int(img->size.width),
      fmt_int(img->size.height));
}
#endif

static VkAttachmentLoadOp rvk_attach_color_load_op(const RvkPass* pass, const u32 idx) {
  (void)idx;
  if (pass->flags & RvkPassFlags_ColorClear) {
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  }
  if (pass->flags & RvkPassFlags_ColorLoadTransfer) {
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  }
  return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkImageLayout rvk_attach_color_initial_layout(const RvkPass* pass, const u32 idx) {
  (void)idx;
  if (pass->flags & RvkPassFlags_ColorLoadTransfer) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_UNDEFINED;
}

static VkAttachmentLoadOp rvk_attach_depth_load_op(const RvkPass* pass) {
  if (pass->flags & RvkPassFlags_DepthClear) {
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  }
  if (pass->flags & RvkPassFlags_DepthLoadTransfer) {
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  }
  return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp rvk_attach_depth_store_op(const RvkPass* pass) {
  if (pass->flags & RvkPassFlags_DepthStore) {
    return VK_ATTACHMENT_STORE_OP_STORE;
  }
  return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkImageLayout rvk_attach_depth_initial_layout(const RvkPass* pass) {
  if (pass->flags & RvkPassFlags_DepthLoadTransfer) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_UNDEFINED;
}

static VkRenderPass rvk_renderpass_create(const RvkPass* pass) {
  VkAttachmentDescription attachments[pass_attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[pass_attachment_color_max];
  VkAttachmentReference   depthRef;
  bool                    hasDepthRef = false;

  for (u32 i = 0; i != rvk_attach_color_count(pass->flags); ++i) {
    attachments[attachmentCount++] = (VkAttachmentDescription){
        .format         = rvk_attach_color_format_at_index(pass, i),
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = rvk_attach_color_load_op(pass, i),
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = rvk_attach_color_initial_layout(pass, i),
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    colorRefs[i] = (VkAttachmentReference){
        .attachment = attachmentCount - 1,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
  }

  if (pass->flags & RvkPassFlags_Depth) {
    attachments[attachmentCount++] = (VkAttachmentDescription){
        .format         = pass->dev->vkDepthFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = rvk_attach_depth_load_op(pass),
        .storeOp        = rvk_attach_depth_store_op(pass),
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = rvk_attach_depth_initial_layout(pass),
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    depthRef = (VkAttachmentReference){
        .attachment = attachmentCount - 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    hasDepthRef = true;
  }
  const VkSubpassDescription subpass = {
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount    = rvk_attach_color_count(pass->flags),
      .pColorAttachments       = colorRefs,
      .pDepthStencilAttachment = hasDepthRef ? &depthRef : null,
  };
  VkSubpassDependency dependencies[pass_dependencies_max];
  u32                 dependencyCount = 0;

  /**
   * Synchronize the attachment layout transitions.
   */
  dependencies[dependencyCount++] = (VkSubpassDependency){
      .srcSubpass   = VK_SUBPASS_EXTERNAL,
      .dstSubpass   = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
  };
  if (pass->flags & RvkPassFlags_ColorLoadTransfer) {
    /**
     * Synchronize the transferring to the color attachments with this pass writing to them.
     */
    dependencies[dependencyCount++] = (VkSubpassDependency){
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
  }
  if (pass->flags & RvkPassFlags_DepthLoadTransfer) {
    /**
     * Synchronize the transferring to the depth-buffer with this pass reading / writing to it.
     */
    dependencies[dependencyCount++] = (VkSubpassDependency){
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
  }
  const VkRenderPassCreateInfo renderPassInfo = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachmentCount,
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = dependencyCount,
      .pDependencies   = dependencies,
  };
  VkRenderPass result;
  rvk_call(vkCreateRenderPass, pass->dev->vkDev, &renderPassInfo, &pass->dev->vkAlloc, &result);
  return result;
}

/**
 * Create a pipeline layout with a single global descriptor-set 0.
 * All pipeline layouts have to be compatible with this layout.
 * This allows us to share the global data binding between different pipelines.
 */
static VkPipelineLayout rvk_global_layout_create(RvkDevice* dev, const RvkDescMeta* descMeta) {
  const VkDescriptorSetLayout      sets[] = {rvk_desc_vklayout(dev->descPool, descMeta)};
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = array_elems(sets),
      .pSetLayouts    = sets,
  };
  VkPipelineLayout result;
  rvk_call(vkCreatePipelineLayout, dev->vkDev, &pipelineLayoutInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFramebuffer rvk_framebuffer_create(RvkPass* pass) {
  VkImageView attachments[pass_attachment_max];
  u32         attachCount = 0;
  for (u32 i = 0; i != rvk_attach_color_count(pass->flags); ++i) {
    diag_assert_msg(
        pass->attachColors[i],
        "Pass {} is missing color attachment {}",
        fmt_text(pass->name),
        fmt_int(i));
    attachments[attachCount++] = pass->attachColors[i]->vkImageView;
  }
  if (pass->flags & RvkPassFlags_Depth) {
    diag_assert_msg(
        pass->attachDepth, "Pass {} is missing a depth attachment", fmt_text(pass->name));
    attachments[attachCount++] = pass->attachDepth->vkImageView;
  }

  const VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = pass->vkRendPass,
      .attachmentCount = attachCount,
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

static void rvk_pass_bind_global(RvkPass* pass) {
  diag_assert(pass->globalBoundMask != 0);

  const VkDescriptorSet descSets[]       = {rvk_desc_set_vkset(pass->globalDescSet)};
  const u32             dynamicOffsets[] = {pass->globalDataOffset};
  vkCmdBindDescriptorSets(
      pass->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pass->globalPipelineLayout,
      RvkGraphicSet_Global,
      array_elems(descSets),
      descSets,
      array_elems(dynamicOffsets),
      dynamicOffsets);
}

static void rvk_pass_vkrenderpass_begin(
    RvkPass* pass, RvkPassInvoc* invoc, const RvkSize size, const GeoColor clearColor) {

  if (pass->flags & RvkPassFlags_ColorLoadTransfer) {
    for (u32 i = 0; i != rvk_attach_color_count(pass->flags); ++i) {
      diag_assert_msg(
          pass->attachColors[i] && pass->attachColors[i]->phase == RvkImagePhase_TransferDest,
          "Pass {} unable to load the color{} from transfer: Unexpected image phase",
          fmt_text(pass->name),
          fmt_int(i));
    }
  }
  if (pass->flags & RvkPassFlags_DepthLoadTransfer) {
    diag_assert_msg(
        pass->attachDepth && pass->attachDepth->phase == RvkImagePhase_TransferDest,
        "Pass {} unable to load the depth from transfer: Unexpected image phase",
        fmt_text(pass->name));
  }

  VkClearValue clearValues[pass_attachment_max];
  u32          clearValueCount = 0;

  if (pass->flags & (RvkPassFlags_ColorClear | RvkPassFlags_DepthClear)) {
    for (u32 i = 0; i != rvk_attach_color_count(pass->flags); ++i) {
      clearValues[clearValueCount++].color = *(VkClearColorValue*)&clearColor;
    }
    if (pass->flags & RvkPassFlags_Depth) {
      // Init depth to zero for a reversed-z depth-buffer.
      clearValues[clearValueCount++].depthStencil = (VkClearDepthStencilValue){.depth = 0.0f};
    }
  }

  const VkRenderPassBeginInfo renderPassInfo = {
      .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass               = pass->vkRendPass,
      .framebuffer              = invoc->vkFrameBuffer,
      .renderArea.offset        = {0, 0},
      .renderArea.extent.width  = size.width,
      .renderArea.extent.height = size.height,
      .clearValueCount          = clearValueCount,
      .pClearValues             = clearValues,
  };
  vkCmdBeginRenderPass(pass->vkCmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
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
  diag_assert_msg(mesh->flags & RvkMeshFlags_Ready, "Mesh is not ready for binding");

  const RvkDescMeta meta    = rvk_pass_meta_dynamic(pass);
  const RvkDescSet  descSet = rvk_pass_alloc_dyn_desc(pass, &meta);
  rvk_desc_set_attach_buffer(descSet, 0, &mesh->vertexBuffer, 0);

  const VkDescriptorSet vkDescSets[] = {rvk_desc_set_vkset(descSet)};
  vkCmdBindDescriptorSets(
      pass->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      graphic->vkPipelineLayout,
      RvkGraphicSet_Dynamic,
      array_elems(vkDescSets),
      vkDescSets,
      0,
      null);

  vkCmdBindIndexBuffer(
      pass->vkCmdBuf,
      mesh->indexBuffer.vkBuffer,
      0,
      sizeof(AssetMeshIndex) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

static void rvk_pass_free_invocations(RvkPass* pass) {
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) {
    vkDestroyFramebuffer(pass->dev->vkDev, invoc->vkFrameBuffer, &pass->dev->vkAlloc);
  }
  dynarray_clear(&pass->invocations);
}

static RvkPassInvoc* rvk_pass_invoc_begin(RvkPass* pass) {
  pass->flags |= RvkPassPrivateFlags_Active;
  RvkPassInvoc* res = dynarray_push_t(&pass->invocations, RvkPassInvoc);
  *res              = (RvkPassInvoc){0};
  return res;
}

static RvkPassInvoc* rvk_pass_invoc_active(RvkPass* pass) {
  if (!(pass->flags & RvkPassPrivateFlags_Active)) {
    return null;
  }
  return dynarray_at_t(&pass->invocations, pass->invocations.size - 1, RvkPassInvoc);
}

RvkPass* rvk_pass_create(
    RvkDevice*         dev,
    const VkFormat     swapchainFormat,
    VkCommandBuffer    vkCmdBuf,
    RvkUniformPool*    uniformPool,
    RvkStopwatch*      stopwatch,
    const RvkPassFlags flags,
    const String       name) {
  diag_assert(!string_is_empty(name));

  // TODO: These exclusion checks are unwieldy and a clear sign that these should not be flags :)
  diag_assert(!(flags & RvkPassFlags_Color1Srgb) || (flags & RvkPassFlags_Color1));
  diag_assert(!(flags & RvkPassFlags_Color1Swapchain) || (flags & RvkPassFlags_Color1));
  diag_assert(!(flags & RvkPassFlags_Color1Swapchain) || !(flags & RvkPassFlags_Color1Srgb));
  diag_assert(!(flags & RvkPassFlags_Color1Swapchain) || !(flags & RvkPassFlags_Color1Float));
  diag_assert(!(flags & RvkPassFlags_Color1Srgb) || !(flags & RvkPassFlags_Color1Float));
  diag_assert(!(flags & RvkPassFlags_Color1Swapchain) || !(flags & RvkPassFlags_Color1Single));
  diag_assert(!(flags & RvkPassFlags_Color2Srgb) || (flags & RvkPassFlags_Color2));
  diag_assert(!(flags & RvkPassFlags_ColorLoadTransfer) || !(flags & RvkPassFlags_ColorClear));
  diag_assert(!(flags & RvkPassFlags_DepthLoadTransfer) || !(flags & RvkPassFlags_DepthClear));
  diag_assert(!(flags & RvkPassFlags_DepthLoadTransfer) || (flags & RvkPassFlags_Depth));
  diag_assert(!(flags & RvkPassFlags_DepthStore) || (flags & RvkPassFlags_Depth));

  RvkDescMeta globalDescMeta = {
      .bindings[0] = RvkDescKind_UniformBufferDynamic,
  };
  for (u16 globalImgIdx = 0; globalImgIdx != pass_global_image_max; ++globalImgIdx) {
    globalDescMeta.bindings[1 + globalImgIdx] = RvkDescKind_CombinedImageSampler2D;
  }
  const RvkDescSet       globalDescSet        = rvk_desc_alloc(dev->descPool, &globalDescMeta);
  const VkPipelineLayout globalPipelineLayout = rvk_global_layout_create(dev, &globalDescMeta);

  RvkPass* pass = alloc_alloc_t(g_alloc_heap, RvkPass);

  *pass = (RvkPass){
      .dev                  = dev,
      .swapchainFormat      = swapchainFormat,
      .name                 = string_dup(g_alloc_heap, name),
      .statrecorder         = rvk_statrecorder_create(dev),
      .stopwatch            = stopwatch,
      .vkCmdBuf             = vkCmdBuf,
      .uniformPool          = uniformPool,
      .flags                = flags,
      .globalDescSet        = globalDescSet,
      .globalPipelineLayout = globalPipelineLayout,
      .dynDescSets          = dynarray_create_t(g_alloc_heap, RvkDescSet, 64),
      .invocations          = dynarray_create_t(g_alloc_heap, RvkPassInvoc, 1),
  };

  pass->vkRendPass = rvk_renderpass_create(pass);
  rvk_debug_name_pass(dev->debug, pass->vkRendPass, "{}", fmt_text(name));

  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass still active");

  string_free(g_alloc_heap, pass->name);
  rvk_pass_free_dyn_desc(pass);
  rvk_pass_free_invocations(pass);

  rvk_statrecorder_destroy(pass->statrecorder);

  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);

  rvk_desc_free(pass->globalDescSet);
  vkDestroyPipelineLayout(pass->dev->vkDev, pass->globalPipelineLayout, &pass->dev->vkAlloc);
  if (rvk_sampler_initialized(&pass->globalImageSampler)) {
    rvk_sampler_destroy(&pass->globalImageSampler, pass->dev);
  }
  if (rvk_sampler_initialized(&pass->globalShadowSampler)) {
    rvk_sampler_destroy(&pass->globalShadowSampler, pass->dev);
  }
  dynarray_destroy(&pass->dynDescSets);
  dynarray_destroy(&pass->invocations);

  alloc_free_t(g_alloc_heap, pass);
}

bool rvk_pass_active(const RvkPass* pass) {
  return (pass->flags & RvkPassPrivateFlags_Active) != 0;
}

String  rvk_pass_name(const RvkPass* pass) { return pass->name; }
RvkSize rvk_pass_size(const RvkPass* pass) { return pass->size; }

bool rvk_pass_has_depth(const RvkPass* pass) { return (pass->flags & RvkPassFlags_Depth) != 0; }

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass* pass, const u16 colorAttachIndex) {
  RvkImageCapability capabilities = 0;
  if (colorAttachIndex != 0 || !(pass->flags & RvkPassFlags_Color1Swapchain)) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;
  }
  if (pass->flags & RvkPassFlags_ColorLoadTransfer) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferDest;
  }
  return (RvkAttachSpec){
      .vkFormat     = rvk_attach_color_format_at_index(pass, colorAttachIndex),
      .capabilities = capabilities,
  };
}

RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass* pass) {
  RvkImageCapability capabilities = 0;
  if (pass->flags & RvkPassFlags_DepthStore) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;
  }
  if (pass->flags & RvkPassFlags_DepthLoadTransfer) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferDest;
  }
  return (RvkAttachSpec){.vkFormat = pass->dev->vkDepthFormat, .capabilities = capabilities};
}

RvkDescMeta rvk_pass_meta_global(const RvkPass* pass) {
  return rvk_desc_set_meta(pass->globalDescSet);
}

RvkDescMeta rvk_pass_meta_dynamic(const RvkPass* pass) {
  (void)pass;
  /**
   * Single StorageBuffer for the vertices.
   */
  return (RvkDescMeta){.bindings[0] = RvkDescKind_StorageBuffer};
}

RvkDescMeta rvk_pass_meta_draw(const RvkPass* pass) { return rvk_uniform_meta(pass->uniformPool); }

RvkDescMeta rvk_pass_meta_instance(const RvkPass* pass) {
  return rvk_uniform_meta(pass->uniformPool);
}

VkRenderPass rvk_pass_vkrenderpass(const RvkPass* pass) { return pass->vkRendPass; }

u64 rvk_pass_stat(const RvkPass* pass, const RvkStat stat) {
  if (dynarray_size(&pass->invocations) == 0) {
    return 0;
  }
  return rvk_statrecorder_query(pass->statrecorder, stat);
}

TimeDuration rvk_pass_duration(const RvkPass* pass) {
  TimeDuration dur = 0;
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) {
    const TimeSteady timestampBegin = rvk_stopwatch_query(pass->stopwatch, invoc->timeRecBegin);
    const TimeSteady timestampEnd   = rvk_stopwatch_query(pass->stopwatch, invoc->timeRecEnd);
    dur += time_steady_duration(timestampBegin, timestampEnd);
  }
  return dur;
}

void rvk_pass_reset(RvkPass* pass) {
  pass->flags &= ~RvkPassPrivateFlags_Recorded;
  rvk_statrecorder_reset(pass->statrecorder, pass->vkCmdBuf);

  // Clear last frame's bound resources.
  mem_set(array_mem(pass->attachColors), 0);
  pass->attachColorMask = 0;
  pass->attachDepth     = null;
  mem_set(array_mem(pass->globalImages), 0);
  pass->globalBoundMask = 0;

  // Free last frame's resources.
  rvk_pass_free_dyn_desc(pass);
  rvk_pass_free_invocations(pass);
}

void rvk_pass_set_size(RvkPass* pass, const RvkSize size) {
  diag_assert_msg(size.width && size.height, "Pass cannot be zero sized");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");
  diag_assert_msg(!pass->attachColorMask && !pass->attachDepth, "Pass attachments already bound");

  pass->size = size;
}

bool rvk_pass_prepare(RvkPass* pass, RvkGraphic* graphic) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  return rvk_graphic_prepare(graphic, pass->vkCmdBuf, pass);
}

bool rvk_pass_prepare_mesh(MAYBE_UNUSED RvkPass* pass, RvkMesh* mesh) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  return rvk_mesh_prepare(mesh);
}

void rvk_pass_bind_attach_color(RvkPass* pass, RvkImage* img, const u16 idx) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");
  diag_assert_msg(idx < rvk_attach_color_count(pass->flags), "Invalid color attachment-index");
  diag_assert_msg(!pass->attachColors[idx], "Color attachment already bound");

#ifndef VOLO_FAST
  rvk_attach_assert_color(pass, idx, img);
#endif

  pass->attachColors[idx] = img;
  pass->attachColorMask |= 1 << idx;
}

void rvk_pass_bind_attach_depth(RvkPass* pass, RvkImage* img) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");
  diag_assert_msg(!pass->attachDepth, "Depth attachment already bound");
  diag_assert_msg(img->size.data == pass->size.data, "Invalid attachment size");

#ifndef VOLO_FAST
  rvk_attach_assert_depth(pass, img);
#endif

  pass->attachDepth = img;
}

void rvk_pass_bind_global_data(RvkPass* pass, const Mem data) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  const u32 globalDataBinding = 0;
  diag_assert_msg(!(pass->globalBoundMask & (1 << globalDataBinding)), "Data already bound");

  const RvkUniformHandle dataHandle = rvk_uniform_upload(pass->uniformPool, data);
  const RvkBuffer*       dataBuffer = rvk_uniform_buffer(pass->uniformPool, dataHandle);

  rvk_desc_set_attach_buffer(
      pass->globalDescSet, globalDataBinding, dataBuffer, rvk_uniform_size_max(pass->uniformPool));

  pass->globalDataOffset = dataHandle.offset;
  pass->globalBoundMask |= 1 << globalDataBinding;
}

void rvk_pass_bind_global_image(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  const u32 bindIndex = 1 + imageIndex;
  diag_assert_msg(!(pass->globalBoundMask & (1 << bindIndex)), "Image already bound");
  diag_assert_msg(imageIndex < pass_global_image_max, "Global image index out of bounds");
  diag_assert_msg(image->caps & RvkImageCapability_Sampled, "Image does not support sampling");

  if (!rvk_sampler_initialized(&pass->globalImageSampler)) {
    const u8 mipLevels       = 1;
    pass->globalImageSampler = rvk_sampler_create(
        pass->dev,
        RvkSamplerFlags_None,
        RvkSamplerWrap_Clamp,
        RvkSamplerFilter_Linear,
        RvkSamplerAniso_None,
        mipLevels);
    rvk_debug_name_sampler(pass->dev->debug, pass->globalImageSampler.vkSampler, "global_image");
  }

  rvk_desc_set_attach_sampler(pass->globalDescSet, bindIndex, image, &pass->globalImageSampler);

  pass->globalBoundMask |= 1 << bindIndex;
  pass->globalImages[imageIndex] = image;
}

void rvk_pass_bind_global_shadow(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  const u32 bindIndex = 1 + imageIndex;
  diag_assert_msg(!(pass->globalBoundMask & (1 << bindIndex)), "Image already bound");
  diag_assert_msg(imageIndex < pass_global_image_max, "Global image index out of bounds");
  diag_assert_msg(image->type == RvkImageType_DepthAttachment, "Shadow image not a depth-image");
  diag_assert_msg(image->caps & RvkImageCapability_Sampled, "Image does not support sampling");

  if (!rvk_sampler_initialized(&pass->globalShadowSampler)) {
    const u8 mipLevels        = 1;
    pass->globalShadowSampler = rvk_sampler_create(
        pass->dev,
        RvkSamplerFlags_SupportCompare, // Enable support for sampler2DShadow.
        RvkSamplerWrap_Zero,
        RvkSamplerFilter_Linear,
        RvkSamplerAniso_None,
        mipLevels);
    rvk_debug_name_sampler(pass->dev->debug, pass->globalShadowSampler.vkSampler, "global_shadow");
  }

  rvk_desc_set_attach_sampler(pass->globalDescSet, bindIndex, image, &pass->globalShadowSampler);

  pass->globalBoundMask |= 1 << bindIndex;
  pass->globalImages[imageIndex] = image;
}

void rvk_pass_begin(RvkPass* pass, const GeoColor clearColor) {
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Recorded), "Pass already recorded");
  diag_assert_msg(!(pass->flags & RvkPassPrivateFlags_Active), "Pass already active");

  RvkPassInvoc* invoc  = rvk_pass_invoc_begin(pass);
  invoc->vkFrameBuffer = rvk_framebuffer_create(pass);

  rvk_statrecorder_start(pass->statrecorder, pass->vkCmdBuf);

  invoc->timeRecBegin = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);
  rvk_debug_label_begin(
      pass->dev->debug, pass->vkCmdBuf, geo_color_blue, "pass_{}", fmt_text(pass->name));

  // Transition all global images to ShaderRead.
  for (u32 i = 0; i != pass_global_image_max; ++i) {
    if (pass->globalImages[i]) {
      rvk_image_transition(pass->globalImages[i], pass->vkCmdBuf, RvkImagePhase_ShaderRead);
    }
  }

  rvk_pass_vkrenderpass_begin(pass, invoc, pass->size, clearColor);

  // Update attachments to reflect the transitions applied by the render-pass itself.
  for (u32 i = 0; i != rvk_attach_color_count(pass->flags); ++i) {
    rvk_image_transition_external(pass->attachColors[i], RvkImagePhase_ColorAttachment);
  }
  if (pass->flags & RvkPassFlags_Depth) {
    rvk_image_transition_external(pass->attachDepth, RvkImagePhase_DepthAttachment);
  }

  rvk_pass_viewport_set(pass->vkCmdBuf, pass->size);
  rvk_pass_scissor_set(pass->vkCmdBuf, pass->size);

  if (pass->globalBoundMask != 0) {
    rvk_pass_bind_global(pass);
  }
}

static u32 rvk_pass_instances_per_draw(RvkPass* pass, const u32 remaining, const u32 dataStride) {
  if (!dataStride) {
    return math_min(remaining, pass_instance_count_max);
  }
  const u32 instances = math_min(remaining, rvk_uniform_size_max(pass->uniformPool) / dataStride);
  return math_min(instances, pass_instance_count_max);
}

void rvk_pass_draw(RvkPass* pass, const RvkPassDraw* draw) {
  diag_assert_msg(pass->flags & RvkPassPrivateFlags_Active, "Pass not active");

  RvkGraphic* graphic           = draw->graphic;
  const u16   reqGlobalBindings = graphic->globalBindings;

  if (UNLIKELY(graphic->outputMask != pass->attachColorMask)) {
    log_e(
        "Graphic output does not match bound color attachments",
        log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY((reqGlobalBindings & pass->globalBoundMask) != reqGlobalBindings)) {
    log_e(
        "Graphic requires additional global bindings",
        log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireDynamicMesh && !draw->dynMesh)) {
    log_e("Graphic requires a dynamic mesh", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireDrawData && !draw->drawData.size)) {
    log_e("Graphic requires draw data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireInstanceData && !draw->instDataStride)) {
    log_e("Graphic requires instance data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(draw->instDataStride > rvk_uniform_size_max(pass->uniformPool))) {
    log_e(
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
        rvk_uniform_upload(pass->uniformPool, draw->drawData),
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
          rvk_uniform_upload(pass->uniformPool, mem_slice(draw->instData, dataOffset, dataSize)),
          pass->vkCmdBuf,
          graphic->vkPipelineLayout,
          RvkGraphicSet_Instance);
      dataOffset += dataSize;
    }

    if (draw->dynMesh || graphic->mesh) {
      const u32 indexCount = draw->dynMesh ? draw->dynMesh->indexCount : graphic->mesh->indexCount;
      vkCmdDrawIndexed(pass->vkCmdBuf, indexCount, instCount, 0, 0, 0);
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

void rvk_pass_end(RvkPass* pass) {
  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(invoc, "Pass not active");

  pass->flags &= ~RvkPassPrivateFlags_Active;
  pass->flags |= RvkPassPrivateFlags_Recorded;

  rvk_statrecorder_stop(pass->statrecorder, pass->vkCmdBuf);
  vkCmdEndRenderPass(pass->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
  invoc->timeRecEnd = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);
}
