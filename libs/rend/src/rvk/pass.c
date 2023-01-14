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
#define pass_attachment_max (rvk_pass_attach_color_max + 1)
#define pass_dependencies_max 8
#define pass_global_image_max 5

typedef RvkGraphic* RvkGraphicPtr;

typedef enum {
  RvkPassFlags_Active     = 1 << 0,
  RvkPassFlags_NeedsClear = 1 << 1,
} RvkPassFlags;

typedef struct {
  RvkSize            size;
  RvkStopwatchRecord timeRecBegin, timeRecEnd;
  VkFramebuffer      vkFrameBuffer;
} RvkPassInvoc;

typedef struct {
  RvkSize  size;
  GeoColor clearColor;

  // Attachments.
  RvkImage* attachColors[rvk_pass_attach_color_max];
  RvkImage* attachDepth;
  u16       attachColorMask;

  // Global resources.
  u32       globalDataOffset;
  u16       globalBoundMask; // Bitset of the bound global resources.
  RvkImage* globalImages[pass_global_image_max];
} RvkPassStage;

/**
 * Stage is a place to build-up the pass state before beginning a render pass.
 */
static RvkPassStage* rvk_pass_stage() {
  static THREAD_LOCAL RvkPassStage g_stage;
  return &g_stage;
}

struct sRvkPass {
  RvkDevice*       dev;
  VkFormat         swapchainFormat;
  RvkStatRecorder* statrecorder;
  RvkStopwatch*    stopwatch;
  RvkPassConfig    config;
  String           name;
  VkRenderPass     vkRendPass;
  VkCommandBuffer  vkCmdBuf;
  RvkUniformPool*  uniformPool;

  RvkPassFlags flags;

  VkPipelineLayout globalPipelineLayout;
  RvkDescSet       globalDescSet;
  RvkSampler       globalImageSampler, globalShadowSampler;

  DynArray dynDescSets; // RvkDescSet[]
  DynArray invocations; // RvkPassInvoc[]
};

static VkFormat rvk_attach_color_format(const RvkPass* pass, const u32 index) {
  diag_assert(index < rvk_pass_attach_color_max);
  const RvkPassFormat format = pass->config.attachColorFormat[index];
  switch (format) {
  case RvkPassFormat_None:
    diag_crash_msg("Pass has no color attachment at index: {}", fmt_int(index));
  case RvkPassFormat_Color1Linear:
    return VK_FORMAT_R8_UNORM;
  case RvkPassFormat_Color4Linear:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RvkPassFormat_Color4Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case RvkPassFormat_Color3Float:
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  case RvkPassFormat_Swapchain:
    return pass->swapchainFormat;
  }
  diag_crash_msg("Unsupported pass color attachment format");
}

static u32 rvk_attach_color_count(const RvkPassConfig* config) {
  u32 result = 0;
  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    result += config->attachColorFormat[i] != RvkPassFormat_None;
  }
  return result;
}

#ifndef VOLO_FAST
static void rvk_attach_assert_color(const RvkPass* pass, const u32 idx, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_color(pass, idx);
  if (pass->config.attachColorFormat[idx] == RvkPassFormat_Swapchain) {
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
}

static void rvk_pass_assert_image_contents(const RvkPass* pass, const RvkPassStage* stage) {
  // Validate preserved color attachment contents.
  for (u32 i = 0; i != rvk_attach_color_count(&pass->config); ++i) {
    if (pass->config.attachColorLoad[i] == RvkPassLoad_Preserve) {
      diag_assert_msg(
          stage->attachColors[i]->phase,
          "Pass {} preserved color attachment {} has undefined contents",
          fmt_text(pass->name),
          fmt_int(i));
    }
  }
  // Validate preserved depth attachment contents.
  if (pass->config.attachDepthLoad == RvkPassLoad_Preserve) {
    diag_assert_msg(
        stage->attachDepth->phase,
        "Pass {} preserved depth attachment has undefined contents",
        fmt_text(pass->name));
  }
  // Validate global image contents.
  for (u32 i = 0; i != pass_global_image_max; ++i) {
    if (stage->globalImages[i]) {
      diag_assert_msg(
          stage->globalImages[i]->phase,
          "Pass {} global image {} has undefined contents",
          fmt_text(pass->name),
          fmt_int(i));
    }
  }
}
#endif // !VOLO_FAST

static VkAttachmentLoadOp rvk_attach_color_load_op(const RvkPass* pass, const u32 idx) {
  switch (pass->config.attachColorLoad[idx]) {
  case RvkPassLoad_Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case RvkPassLoad_Preserve:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  default:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

static VkAttachmentLoadOp rvk_attach_depth_load_op(const RvkPass* pass) {
  switch (pass->config.attachDepthLoad) {
  case RvkPassLoad_Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case RvkPassLoad_Preserve:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  default:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

static VkAttachmentStoreOp rvk_attach_depth_store_op(const RvkPass* pass) {
  return pass->config.attachDepth == RvkPassDepth_Stored ? VK_ATTACHMENT_STORE_OP_STORE
                                                         : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkRenderPass rvk_renderpass_create(const RvkPass* pass) {
  VkAttachmentDescription attachments[pass_attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[rvk_pass_attach_color_max];
  VkAttachmentReference   depthRef;
  bool                    hasDepthRef = false;

  for (u32 i = 0; i != rvk_attach_color_count(&pass->config); ++i) {
    attachments[attachmentCount++] = (VkAttachmentDescription){
        .format         = rvk_attach_color_format(pass, i),
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = rvk_attach_color_load_op(pass, i),
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    colorRefs[i] = (VkAttachmentReference){
        .attachment = attachmentCount - 1,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
  }

  if (pass->config.attachDepth) {
    attachments[attachmentCount++] = (VkAttachmentDescription){
        .format         = pass->dev->vkDepthFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = rvk_attach_depth_load_op(pass),
        .storeOp        = rvk_attach_depth_store_op(pass),
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
      .colorAttachmentCount    = rvk_attach_color_count(&pass->config),
      .pColorAttachments       = colorRefs,
      .pDepthStencilAttachment = hasDepthRef ? &depthRef : null,
  };

  const VkRenderPassCreateInfo renderPassInfo = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachmentCount,
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
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

static VkFramebuffer rvk_framebuffer_create(RvkPass* pass, RvkPassStage* stage) {
  VkImageView attachments[pass_attachment_max];
  u32         attachCount = 0;
  for (u32 i = 0; i != rvk_attach_color_count(&pass->config); ++i) {
    diag_assert_msg(
        stage->attachColors[i],
        "Pass {} is missing color attachment {}",
        fmt_text(pass->name),
        fmt_int(i));
    attachments[attachCount++] = stage->attachColors[i]->vkImageView;
  }
  if (pass->config.attachDepth) {
    diag_assert_msg(
        stage->attachDepth, "Pass {} is missing a depth attachment", fmt_text(pass->name));
    attachments[attachCount++] = stage->attachDepth->vkImageView;
  }

  const VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = pass->vkRendPass,
      .attachmentCount = attachCount,
      .pAttachments    = attachments,
      .width           = stage->size.width,
      .height          = stage->size.height,
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

static void rvk_pass_bind_global(RvkPass* pass, RvkPassStage* stage) {
  diag_assert(stage->globalBoundMask != 0);

  const VkDescriptorSet descSets[]       = {rvk_desc_set_vkset(pass->globalDescSet)};
  const u32             dynamicOffsets[] = {stage->globalDataOffset};
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

static void rvk_pass_vkrenderpass_begin(RvkPass* pass, RvkPassInvoc* invoc, RvkPassStage* stage) {
  VkClearValue clearValues[pass_attachment_max];
  u32          clearValueCount = 0;

  if (pass->flags & RvkPassFlags_NeedsClear) {
    for (u32 i = 0; i != rvk_attach_color_count(&pass->config); ++i) {
      clearValues[clearValueCount++].color = *(VkClearColorValue*)&stage->clearColor;
    }
    if (pass->config.attachDepth) {
      // Init depth to zero for a reversed-z depth-buffer.
      clearValues[clearValueCount++].depthStencil = (VkClearDepthStencilValue){.depth = 0.0f};
    }
  }

  const VkRenderPassBeginInfo renderPassInfo = {
      .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass               = pass->vkRendPass,
      .framebuffer              = invoc->vkFrameBuffer,
      .renderArea.offset        = {0, 0},
      .renderArea.extent.width  = invoc->size.width,
      .renderArea.extent.height = invoc->size.height,
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
  pass->flags |= RvkPassFlags_Active;
  RvkPassInvoc* res = dynarray_push_t(&pass->invocations, RvkPassInvoc);
  *res              = (RvkPassInvoc){0};
  return res;
}

static RvkPassInvoc* rvk_pass_invoc_active(RvkPass* pass) {
  if (!(pass->flags & RvkPassFlags_Active)) {
    return null;
  }
  return dynarray_at_t(&pass->invocations, pass->invocations.size - 1, RvkPassInvoc);
}

RvkPass* rvk_pass_create(
    RvkDevice*          dev,
    const VkFormat      swapchainFormat,
    VkCommandBuffer     vkCmdBuf,
    RvkUniformPool*     uniformPool,
    RvkStopwatch*       stopwatch,
    const RvkPassConfig config,
    const String        name) {
  diag_assert(!string_is_empty(name));

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
      .config               = config,
      .globalDescSet        = globalDescSet,
      .globalPipelineLayout = globalPipelineLayout,
      .dynDescSets          = dynarray_create_t(g_alloc_heap, RvkDescSet, 64),
      .invocations          = dynarray_create_t(g_alloc_heap, RvkPassInvoc, 1),
  };

  pass->vkRendPass = rvk_renderpass_create(pass);
  rvk_debug_name_pass(dev->debug, pass->vkRendPass, "{}", fmt_text(name));

  bool anyAttachmentNeedsClear = pass->config.attachDepthLoad == RvkPassLoad_Clear;
  array_for_t(pass->config.attachColorLoad, RvkPassLoad, load) {
    anyAttachmentNeedsClear |= *load == RvkPassLoad_Clear;
  }
  if (anyAttachmentNeedsClear) {
    pass->flags |= RvkPassFlags_NeedsClear;
  }

  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation still active");

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

bool rvk_pass_active(const RvkPass* pass) { return rvk_pass_invoc_active((RvkPass*)pass) != null; }

String rvk_pass_name(const RvkPass* pass) { return pass->name; }

bool rvk_pass_has_depth(const RvkPass* pass) {
  return pass->config.attachDepth != RvkPassDepth_None;
}

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass* pass, const u16 colorAttachIndex) {
  RvkImageCapability capabilities = 0;
  if (pass->config.attachColorFormat[colorAttachIndex] != RvkPassFormat_Swapchain) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;
  }
  if (pass->config.attachColorLoad[colorAttachIndex] == RvkPassLoad_Preserve) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferDest;
  }
  return (RvkAttachSpec){
      .vkFormat     = rvk_attach_color_format(pass, colorAttachIndex),
      .capabilities = capabilities,
  };
}

RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass* pass) {
  RvkImageCapability capabilities = 0;
  if (pass->config.attachDepth == RvkPassDepth_Stored) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;
  }
  if (pass->config.attachDepthLoad == RvkPassLoad_Preserve) {
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

u16 rvk_pass_stat_invocations(const RvkPass* pass) {
  return (u16)dynarray_size(&pass->invocations);
}

RvkSize rvk_pass_stat_size_max(const RvkPass* pass) {
  RvkSize size = {0};
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) {
    size.width  = math_max(size.width, invoc->size.width);
    size.height = math_max(size.height, invoc->size.height);
  }
  return size;
}

TimeDuration rvk_pass_stat_duration(const RvkPass* pass) {
  TimeDuration dur = 0;
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) {
    const TimeSteady timestampBegin = rvk_stopwatch_query(pass->stopwatch, invoc->timeRecBegin);
    const TimeSteady timestampEnd   = rvk_stopwatch_query(pass->stopwatch, invoc->timeRecEnd);
    dur += time_steady_duration(timestampBegin, timestampEnd);
  }
  return dur;
}

void rvk_pass_reset(RvkPass* pass) {
  rvk_statrecorder_reset(pass->statrecorder, pass->vkCmdBuf);
  rvk_pass_free_dyn_desc(pass);
  rvk_pass_free_invocations(pass);
  *rvk_pass_stage() = (RvkPassStage){0}; // Reset the stage.
}

bool rvk_pass_prepare(RvkPass* pass, RvkGraphic* graphic) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  return rvk_graphic_prepare(graphic, pass->vkCmdBuf, pass);
}

bool rvk_pass_prepare_mesh(MAYBE_UNUSED RvkPass* pass, RvkMesh* mesh) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  return rvk_mesh_prepare(mesh);
}

void rvk_pass_stage_clear_color(MAYBE_UNUSED RvkPass* pass, const GeoColor clearColor) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  rvk_pass_stage()->clearColor = clearColor;
}

void rvk_pass_stage_attach_color(MAYBE_UNUSED RvkPass* pass, RvkImage* img, const u16 idx) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");
  diag_assert_msg(idx < rvk_attach_color_count(&pass->config), "Invalid color attachment-index");

  RvkPassStage* stage = rvk_pass_stage();
  diag_assert_msg(!stage->attachColors[idx], "Color attachment already bound");

  if (!stage->size.data) {
    stage->size = img->size;
  } else {
    diag_assert_msg(
        img->size.data == stage->size.data,
        "Pass {} color attachment {} invalid: Invalid size (expected: {}x{}, actual: {}x{})",
        fmt_text(pass->name),
        fmt_int(idx),
        fmt_int(stage->size.width),
        fmt_int(stage->size.height),
        fmt_int(img->size.width),
        fmt_int(img->size.height));
  }

#ifndef VOLO_FAST
  rvk_attach_assert_color(pass, idx, img);
#endif

  stage->attachColors[idx] = img;
  stage->attachColorMask |= 1 << idx;
}

void rvk_pass_stage_attach_depth(MAYBE_UNUSED RvkPass* pass, RvkImage* img) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();
  diag_assert_msg(!stage->attachDepth, "Depth attachment already bound");

  if (!stage->size.data) {
    stage->size = img->size;
  } else {
    diag_assert_msg(
        img->size.data == stage->size.data,
        "Pass {} depth attachment invalid: Invalid size (expected: {}x{}, actual: {}x{})",
        fmt_text(pass->name),
        fmt_int(stage->size.width),
        fmt_int(stage->size.height),
        fmt_int(img->size.width),
        fmt_int(img->size.height));
  }

#ifndef VOLO_FAST
  rvk_attach_assert_depth(pass, img);
#endif

  stage->attachDepth = img;
}

void rvk_pass_stage_global_data(MAYBE_UNUSED RvkPass* pass, const Mem data) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();

  const u32 globalDataBinding = 0;
  diag_assert_msg(!(stage->globalBoundMask & (1 << globalDataBinding)), "Data already staged");

  const RvkUniformHandle dataHandle = rvk_uniform_upload(pass->uniformPool, data);
  const RvkBuffer*       dataBuffer = rvk_uniform_buffer(pass->uniformPool, dataHandle);

  rvk_desc_set_attach_buffer(
      pass->globalDescSet, globalDataBinding, dataBuffer, rvk_uniform_size_max(pass->uniformPool));

  stage->globalDataOffset = dataHandle.offset;
  stage->globalBoundMask |= 1 << globalDataBinding;
}

void rvk_pass_stage_global_image(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();

  const u32 bindIndex = 1 + imageIndex;
  diag_assert_msg(!(stage->globalBoundMask & (1 << bindIndex)), "Image already staged");
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

  stage->globalBoundMask |= 1 << bindIndex;
  stage->globalImages[imageIndex] = image;
}

void rvk_pass_stage_global_shadow(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();

  const u32 bindIndex = 1 + imageIndex;
  diag_assert_msg(!(stage->globalBoundMask & (1 << bindIndex)), "Image already staged");
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

  stage->globalBoundMask |= 1 << bindIndex;
  stage->globalImages[imageIndex] = image;
}

void rvk_pass_begin(RvkPass* pass) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage  = rvk_pass_stage();
  RvkPassInvoc* invoc  = rvk_pass_invoc_begin(pass);
  invoc->size          = stage->size;
  invoc->vkFrameBuffer = rvk_framebuffer_create(pass, stage);

#ifndef VOLO_FAST
  // Validate that all images we load have contents loaded in them.
  rvk_pass_assert_image_contents(pass, stage);
#endif

  rvk_statrecorder_start(pass->statrecorder, pass->vkCmdBuf);

  invoc->timeRecBegin = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);
  rvk_debug_label_begin(
      pass->dev->debug, pass->vkCmdBuf, geo_color_blue, "pass_{}", fmt_text(pass->name));

  // Transition all attachment images to color/depth-attachment-optimal.
  for (u32 i = 0; i != rvk_attach_color_count(&pass->config); ++i) {
    rvk_image_transition(stage->attachColors[i], pass->vkCmdBuf, RvkImagePhase_ColorAttachment);
  }
  if (pass->config.attachDepth) {
    rvk_image_transition(stage->attachDepth, pass->vkCmdBuf, RvkImagePhase_DepthAttachment);
  }

  // Transition all global images to ShaderRead.
  for (u32 i = 0; i != pass_global_image_max; ++i) {
    if (stage->globalImages[i]) {
      rvk_image_transition(stage->globalImages[i], pass->vkCmdBuf, RvkImagePhase_ShaderRead);
    }
  }

  rvk_pass_vkrenderpass_begin(pass, invoc, stage);

  rvk_pass_viewport_set(pass->vkCmdBuf, invoc->size);
  rvk_pass_scissor_set(pass->vkCmdBuf, invoc->size);

  if (stage->globalBoundMask != 0) {
    rvk_pass_bind_global(pass, stage);
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
  diag_assert_msg(rvk_pass_invoc_active(pass), "Pass invocation not active");

  RvkPassStage* stage             = rvk_pass_stage();
  RvkGraphic*   graphic           = draw->graphic;
  const u16     reqGlobalBindings = graphic->globalBindings;

  if (UNLIKELY(graphic->outputMask != stage->attachColorMask)) {
    log_e(
        "Graphic output does not match bound color attachments",
        log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY((reqGlobalBindings & stage->globalBoundMask) != reqGlobalBindings)) {
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
  RvkPassStage* stage = rvk_pass_stage();
  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(invoc, "Pass not active");

  pass->flags &= ~RvkPassFlags_Active;

  rvk_statrecorder_stop(pass->statrecorder, pass->vkCmdBuf);
  vkCmdEndRenderPass(pass->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
  invoc->timeRecEnd = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);

  if (stage->attachDepth && pass->config.attachDepth != RvkPassDepth_Stored) {
    // When we're not storing the depth, the image's contents become undefined.
    rvk_image_transition_external(stage->attachDepth, RvkImagePhase_Undefined);
  }

  *stage = (RvkPassStage){0}; // Reset the stage.
}
