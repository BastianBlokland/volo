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
#include "repository_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "texture_internal.h"
#include "uniform_internal.h"

#define pass_instance_count_max 2048
#define pass_attachment_max (rvk_pass_attach_color_max + 1)
#define pass_global_data_max 1
#define pass_global_image_max 5
#define pass_draw_image_max 5

typedef RvkGraphic* RvkGraphicPtr;

typedef enum {
  RvkPassFlags_Active     = 1 << 0,
  RvkPassFlags_NeedsClear = 1 << 1,
} RvkPassFlags;

typedef struct {
  RvkSize       size;
  VkFramebuffer vkFrameBuffer;

  u16 drawCount;
  u32 instanceCount;

  RvkStatRecord      statsRecord;
  RvkStopwatchRecord timeRecBegin, timeRecEnd;
} RvkPassInvoc;

typedef struct {
  RvkSize  size;
  GeoColor clearColor;

  // Attachments.
  RvkImage* attachColors[rvk_pass_attach_color_max];
  RvkImage* attachDepth;
  u16       attachColorMask;

  // Global resources.
  RvkDescSet globalDescSet;
  u16        globalBoundMask; // Bitset of the bound global resources.
  RvkImage*  globalImages[pass_global_image_max];

  // Per-draw resources.
  RvkImage* drawImages[pass_draw_image_max];
} RvkPassStage;

/**
 * Stage is a place to build-up the pass state before beginning a render pass.
 */
static RvkPassStage* rvk_pass_stage(void) {
  static THREAD_LOCAL RvkPassStage g_stage;
  return &g_stage;
}

static VkClearColorValue rvk_rend_clear_color(const GeoColor color) {
  VkClearColorValue result;
  mem_cpy(mem_var(result), mem_var(color));
  return result;
}

struct sRvkPass {
  RvkDevice*       dev;
  VkFormat         swapchainFormat;
  RvkStatRecorder* statrecorder;
  RvkStopwatch*    stopwatch;
  RvkPassConfig    config;
  VkRenderPass     vkRendPass;
  VkCommandBuffer  vkCmdBuf;
  RvkUniformPool*  uniformPool;

  RvkPassFlags flags;

  RvkDescMeta      globalDescMeta;
  VkPipelineLayout globalPipelineLayout;

  DynArray descSetsVolatile; // RvkDescSet[], allocated on-demand and automatically freed at reset.
  DynArray invocations;      // RvkPassInvoc[]
};

static VkFormat rvk_attach_color_format(const RvkPass* pass, const u32 index) {
  diag_assert(index < rvk_pass_attach_color_max);
  const RvkPassFormat format = pass->config.attachColorFormat[index];
  switch (format) {
  case RvkPassFormat_None:
    diag_crash_msg("Pass has no color attachment at index: {}", fmt_int(index));
  case RvkPassFormat_Color1Linear:
    return VK_FORMAT_R8_UNORM;
  case RvkPassFormat_Color2Linear:
    return VK_FORMAT_R8G8_UNORM;
  case RvkPassFormat_Color4Linear:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RvkPassFormat_Color4Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case RvkPassFormat_Color2SignedFloat:
    return VK_FORMAT_R16G16_SFLOAT;
  case RvkPassFormat_Color3Float:
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  case RvkPassFormat_Swapchain:
    return pass->swapchainFormat;
  }
  diag_crash_msg("Unsupported pass color attachment format");
}

static u32 rvk_pass_attach_color_count(const RvkPassConfig* config) {
  u32 result = 0;
  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    result += config->attachColorFormat[i] != RvkPassFormat_None;
  }
  return result;
}

#ifndef VOLO_FAST
static void rvk_pass_attach_assert_color(const RvkPass* pass, const u32 idx, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_color(pass, idx);
  if (pass->config.attachColorFormat[idx] == RvkPassFormat_Swapchain) {
    diag_assert_msg(
        img->type == RvkImageType_Swapchain,
        "Pass {} color attachment {} invalid: Expected a swapchain image",
        fmt_text(pass->config.name),
        fmt_int(idx));
  }
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentColor,
      "Pass {} color attachment {} invalid: Missing AttachmentColor capability",
      fmt_text(pass->config.name),
      fmt_int(idx));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} color attachment {} invalid: Missing capabilities",
      fmt_text(pass->config.name),
      fmt_int(idx));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} color attachment {} invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->config.name),
      fmt_int(idx),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
}

static void rvk_pass_attach_assert_depth(const RvkPass* pass, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_depth(pass);
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentDepth,
      "Pass {} depth attachment invalid: Missing AttachmentDepth capability",
      fmt_text(pass->config.name));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} depth attachment invalid: Missing capabilities",
      fmt_text(pass->config.name));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} depth attachment invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->config.name),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
}

static void rvk_pass_assert_image_contents(const RvkPass* pass, const RvkPassStage* stage) {
  // Validate preserved color attachment contents.
  for (u32 i = 0; i != rvk_pass_attach_color_count(&pass->config); ++i) {
    if (pass->config.attachColorLoad[i] == RvkPassLoad_Preserve) {
      diag_assert_msg(
          stage->attachColors[i]->phase,
          "Pass {} preserved color attachment {} has undefined contents",
          fmt_text(pass->config.name),
          fmt_int(i));
    }
  }
  // Validate preserved depth attachment contents.
  if (pass->config.attachDepthLoad == RvkPassLoad_Preserve) {
    diag_assert_msg(
        stage->attachDepth->phase,
        "Pass {} preserved depth attachment has undefined contents",
        fmt_text(pass->config.name));
  }
  // Validate global image contents.
  for (u32 i = 0; i != pass_global_image_max; ++i) {
    if (stage->globalImages[i]) {
      diag_assert_msg(
          stage->globalImages[i]->phase,
          "Pass {} global image {} has undefined contents",
          fmt_text(pass->config.name),
          fmt_int(i));
    }
  }
}

static void rvk_pass_assert_draw_image_staged(const RvkPassStage* stage, const RvkImage* img) {
  for (u32 i = 0; i != pass_draw_image_max; ++i) {
    if (stage->drawImages[i] == img) {
      return; // Image was staged.
    }
  }
  diag_assert_fail("Per-draw image was used but not staged");
}
#endif // !VOLO_FAST

static VkAttachmentLoadOp rvk_pass_attach_color_load_op(const RvkPass* pass, const u32 idx) {
  switch (pass->config.attachColorLoad[idx]) {
  case RvkPassLoad_Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case RvkPassLoad_Preserve:
  case RvkPassLoad_PreserveDontCheck:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  default:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

static VkAttachmentLoadOp rvk_pass_attach_depth_load_op(const RvkPass* pass) {
  switch (pass->config.attachDepthLoad) {
  case RvkPassLoad_Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case RvkPassLoad_Preserve:
  case RvkPassLoad_PreserveDontCheck:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  default:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

static VkAttachmentStoreOp rvk_pass_attach_depth_store_op(const RvkPass* pass) {
  return pass->config.attachDepth == RvkPassDepth_Stored ? VK_ATTACHMENT_STORE_OP_STORE
                                                         : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkRenderPass rvk_renderpass_create(const RvkPass* pass) {
  VkAttachmentDescription attachments[pass_attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[rvk_pass_attach_color_max];
  VkAttachmentReference   depthRef;
  bool                    hasDepthRef = false;

  for (u32 i = 0; i != rvk_pass_attach_color_count(&pass->config); ++i) {
    attachments[attachmentCount++] = (VkAttachmentDescription){
        .format         = rvk_attach_color_format(pass, i),
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = rvk_pass_attach_color_load_op(pass, i),
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
        .loadOp         = rvk_pass_attach_depth_load_op(pass),
        .storeOp        = rvk_pass_attach_depth_store_op(pass),
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
      .colorAttachmentCount    = rvk_pass_attach_color_count(&pass->config),
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

static RvkDescMeta rvk_global_desc_meta(void) {
  RvkDescMeta meta;
  u16         globalBindingCount = 0;
  for (u16 globalDataIdx = 0; globalDataIdx != pass_global_data_max; ++globalDataIdx) {
    meta.bindings[globalBindingCount++] = RvkDescKind_UniformBuffer;
  }
  for (u16 globalImgIdx = 0; globalImgIdx != pass_global_image_max; ++globalImgIdx) {
    meta.bindings[globalBindingCount++] = RvkDescKind_CombinedImageSampler2D;
  }
  return meta;
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
  for (u32 i = 0; i != rvk_pass_attach_color_count(&pass->config); ++i) {
    diag_assert_msg(
        stage->attachColors[i],
        "Pass {} is missing color attachment {}",
        fmt_text(pass->config.name),
        fmt_int(i));
    attachments[attachCount++] = stage->attachColors[i]->vkImageView;
  }
  if (pass->config.attachDepth) {
    diag_assert_msg(
        stage->attachDepth, "Pass {} is missing a depth attachment", fmt_text(pass->config.name));
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

  const VkDescriptorSet vkDescSets[] = {rvk_desc_set_vkset(stage->globalDescSet)};
  vkCmdBindDescriptorSets(
      pass->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pass->globalPipelineLayout,
      RvkGraphicSet_Global,
      array_elems(vkDescSets),
      vkDescSets,
      0,
      null);
}

static void rvk_pass_vkrenderpass_begin(RvkPass* pass, RvkPassInvoc* invoc, RvkPassStage* stage) {
  VkClearValue clearValues[pass_attachment_max];
  u32          clearValueCount = 0;

  if (pass->flags & RvkPassFlags_NeedsClear) {
    for (u32 i = 0; i != rvk_pass_attach_color_count(&pass->config); ++i) {
      clearValues[clearValueCount++].color = rvk_rend_clear_color(stage->clearColor);
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

static void rvk_pass_free_desc_volatile(RvkPass* pass) {
  dynarray_for_t(&pass->descSetsVolatile, RvkDescSet, set) { rvk_desc_free(*set); }
  dynarray_clear(&pass->descSetsVolatile);
}

static RvkDescSet rvk_pass_alloc_desc_volatile(RvkPass* pass, const RvkDescMeta* meta) {
  const RvkDescSet res                                  = rvk_desc_alloc(pass->dev->descPool, meta);
  *dynarray_push_t(&pass->descSetsVolatile, RvkDescSet) = res;
  return res;
}

static void rvk_pass_bind_draw(
    RvkPass*                         pass,
    MAYBE_UNUSED const RvkPassStage* stage,
    RvkGraphic*                      gra,
    const Mem                        data,
    RvkMesh*                         mesh,
    RvkImage*                        img,
    const RvkSamplerSpec             sampler) {
  diag_assert_msg(!mesh || rvk_mesh_is_ready(mesh, pass->dev), "Mesh is not ready for binding");
  diag_assert_msg(!img || img->phase != RvkImagePhase_Undefined, "Image has no content");

  const RvkDescSet descSet = rvk_pass_alloc_desc_volatile(pass, &gra->drawDescMeta);
  if (data.size && gra->drawDescMeta.bindings[0]) {
    const RvkUniformHandle dataHandle = rvk_uniform_upload(pass->uniformPool, data);
    const RvkBuffer*       dataBuffer = rvk_uniform_buffer(pass->uniformPool, dataHandle);
    rvk_desc_set_attach_buffer(descSet, 0, dataBuffer, dataHandle.offset, (u32)data.size);
  }
  if (mesh && gra->drawDescMeta.bindings[1]) {
    rvk_desc_set_attach_buffer(descSet, 1, &mesh->vertexBuffer, 0, 0);
  }
  if (img && gra->drawDescMeta.bindings[2]) {
#ifndef VOLO_FAST
    rvk_pass_assert_draw_image_staged(stage, img);
#endif
    const bool reqCube = gra->drawDescMeta.bindings[2] == RvkDescKind_CombinedImageSamplerCube;
    if (UNLIKELY(reqCube != (img->type == RvkImageType_ColorSourceCube))) {
      log_e("Unsupported draw image type", log_param("graphic", fmt_text(gra->dbgName)));

      const RvkRepositoryId missing =
          reqCube ? RvkRepositoryId_MissingTextureCube : RvkRepositoryId_MissingTexture;
      img = &rvk_repository_texture_get(pass->dev->repository, missing)->image;
    }
    rvk_desc_set_attach_sampler(descSet, 2, img, sampler);
  }

  const VkDescriptorSet vkDescSets[] = {rvk_desc_set_vkset(descSet)};
  vkCmdBindDescriptorSets(
      pass->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      gra->vkPipelineLayout,
      RvkGraphicSet_Draw,
      array_elems(vkDescSets),
      vkDescSets,
      0,
      null);

  if (mesh) {
    vkCmdBindIndexBuffer(
        pass->vkCmdBuf,
        mesh->indexBuffer.vkBuffer,
        0,
        sizeof(AssetMeshIndex) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
  }
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
    const RvkPassConfig config) {
  diag_assert(!string_is_empty(config.name));

  RvkPass* pass = alloc_alloc_t(g_allocHeap, RvkPass);

  *pass = (RvkPass){
      .dev              = dev,
      .swapchainFormat  = swapchainFormat,
      .statrecorder     = rvk_statrecorder_create(dev),
      .stopwatch        = stopwatch,
      .vkCmdBuf         = vkCmdBuf,
      .uniformPool      = uniformPool,
      .config           = config,
      .descSetsVolatile = dynarray_create_t(g_allocHeap, RvkDescSet, 8),
      .invocations      = dynarray_create_t(g_allocHeap, RvkPassInvoc, 1),
  };

  pass->vkRendPass = rvk_renderpass_create(pass);
  rvk_debug_name_pass(dev->debug, pass->vkRendPass, "{}", fmt_text(config.name));

  pass->globalDescMeta       = rvk_global_desc_meta();
  pass->globalPipelineLayout = rvk_global_layout_create(dev, &pass->globalDescMeta);

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

  rvk_pass_free_desc_volatile(pass);
  rvk_pass_free_invocations(pass);

  rvk_statrecorder_destroy(pass->statrecorder);

  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);
  vkDestroyPipelineLayout(pass->dev->vkDev, pass->globalPipelineLayout, &pass->dev->vkAlloc);

  dynarray_destroy(&pass->descSetsVolatile);
  dynarray_destroy(&pass->invocations);

  alloc_free_t(g_allocHeap, pass);
}

bool rvk_pass_active(const RvkPass* pass) { return rvk_pass_invoc_active((RvkPass*)pass) != null; }

String rvk_pass_name(const RvkPass* pass) { return pass->config.name; }

bool rvk_pass_has_depth(const RvkPass* pass) {
  return pass->config.attachDepth != RvkPassDepth_None;
}

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass* pass, const u16 colorAttachIndex) {
  RvkImageCapability capabilities = 0;
  if (pass->config.attachColorFormat[colorAttachIndex] != RvkPassFormat_Swapchain) {
    // TODO: Specifying these capabilities should not be responsibilty of the pass.
    capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;
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
  return (RvkAttachSpec){.vkFormat = pass->dev->vkDepthFormat, .capabilities = capabilities};
}

RvkDescMeta rvk_pass_meta_global(const RvkPass* pass) { return pass->globalDescMeta; }

RvkDescMeta rvk_pass_meta_instance(const RvkPass* pass) {
  (void)pass;
  /**
   * For per-instance data we use a dynamic uniform-buffer fast-path in the UniformPool where it can
   * reuse the same descriptor-sets for different allocations within the same buffer.
   */
  return (RvkDescMeta){
      .bindings[0] = RvkDescKind_UniformBufferDynamic,
  };
}

VkRenderPass rvk_pass_vkrenderpass(const RvkPass* pass) { return pass->vkRendPass; }

u16 rvk_pass_stat_invocations(const RvkPass* pass) {
  return (u16)dynarray_size(&pass->invocations);
}

u16 rvk_pass_stat_draws(const RvkPass* pass) {
  u16 draws = 0;
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) { draws += invoc->drawCount; }
  return draws;
}

u32 rvk_pass_stat_instances(const RvkPass* pass) {
  u32 draws = 0;
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) { draws += invoc->instanceCount; }
  return draws;
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

u64 rvk_pass_stat_pipeline(const RvkPass* pass, const RvkStat stat) {
  u64 res = 0;
  dynarray_for_t(&pass->invocations, RvkPassInvoc, invoc) {
    res += rvk_statrecorder_query(pass->statrecorder, invoc->statsRecord, stat);
  }
  return res;
}

void rvk_pass_reset(RvkPass* pass) {
  rvk_statrecorder_reset(pass->statrecorder, pass->vkCmdBuf);
  rvk_pass_free_desc_volatile(pass);
  rvk_pass_free_invocations(pass);
  *rvk_pass_stage() = (RvkPassStage){0}; // Reset the stage.
}

bool rvk_pass_prepare(RvkPass* pass, RvkGraphic* graphic) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  return rvk_graphic_prepare(graphic, pass);
}

bool rvk_pass_prepare_mesh(MAYBE_UNUSED RvkPass* pass, RvkMesh* mesh) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  return rvk_mesh_is_ready(mesh, pass->dev);
}

bool rvk_pass_prepare_texture(MAYBE_UNUSED RvkPass* pass, RvkTexture* texture) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  return rvk_texture_is_ready(texture, pass->dev);
}

void rvk_pass_stage_clear_color(MAYBE_UNUSED RvkPass* pass, const GeoColor clearColor) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  rvk_pass_stage()->clearColor = clearColor;
}

void rvk_pass_stage_attach_color(MAYBE_UNUSED RvkPass* pass, RvkImage* img, const u16 idx) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");
  diag_assert_msg(
      idx < rvk_pass_attach_color_count(&pass->config), "Invalid color attachment-index");

  RvkPassStage* stage = rvk_pass_stage();
  diag_assert_msg(!stage->attachColors[idx], "Color attachment already bound");

  if (!stage->size.data) {
    stage->size = img->size;
  } else {
    diag_assert_msg(
        img->size.data == stage->size.data,
        "Pass {} color attachment {} invalid: Invalid size (expected: {}x{}, actual: {}x{})",
        fmt_text(pass->config.name),
        fmt_int(idx),
        fmt_int(stage->size.width),
        fmt_int(stage->size.height),
        fmt_int(img->size.width),
        fmt_int(img->size.height));
  }

#ifndef VOLO_FAST
  rvk_pass_attach_assert_color(pass, idx, img);
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
        fmt_text(pass->config.name),
        fmt_int(stage->size.width),
        fmt_int(stage->size.height),
        fmt_int(img->size.width),
        fmt_int(img->size.height));
  }

#ifndef VOLO_FAST
  rvk_pass_attach_assert_depth(pass, img);
#endif

  stage->attachDepth = img;
}

void rvk_pass_stage_global_data(RvkPass* pass, const Mem data, const u16 dataIndex) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();

  const u32 globalDataBinding = dataIndex;
  diag_assert_msg(dataIndex < pass_global_image_max, "Global data index out of bounds");
  diag_assert_msg(!(stage->globalBoundMask & (1 << globalDataBinding)), "Data already staged");

  const RvkUniformHandle dataHandle = rvk_uniform_upload(pass->uniformPool, data);
  const RvkBuffer*       dataBuffer = rvk_uniform_buffer(pass->uniformPool, dataHandle);

  if (!rvk_desc_valid(stage->globalDescSet)) {
    stage->globalDescSet = rvk_pass_alloc_desc_volatile(pass, &pass->globalDescMeta);
  }
  stage->globalBoundMask |= 1 << globalDataBinding;
  rvk_desc_set_attach_buffer(
      stage->globalDescSet, globalDataBinding, dataBuffer, dataHandle.offset, (u32)data.size);
}

static void rvk_pass_stage_global_image_internal(
    RvkPass* pass, RvkImage* image, const u16 imageIndex, const RvkSamplerSpec samplerSpec) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage = rvk_pass_stage();

  if (UNLIKELY(image->type == RvkImageType_ColorSourceCube)) {
    log_e("Cube images cannot be bound globally");
    const RvkRepositoryId missing = RvkRepositoryId_MissingTexture;
    image = &rvk_repository_texture_get(pass->dev->repository, missing)->image;
  }

  const u32 bindIndex = pass_global_data_max + imageIndex;
  diag_assert_msg(!(stage->globalBoundMask & (1 << bindIndex)), "Image already staged");
  diag_assert_msg(imageIndex < pass_global_image_max, "Global image index out of bounds");
  diag_assert_msg(image->caps & RvkImageCapability_Sampled, "Image does not support sampling");

  if (!rvk_desc_valid(stage->globalDescSet)) {
    stage->globalDescSet = rvk_pass_alloc_desc_volatile(pass, &pass->globalDescMeta);
  }
  rvk_desc_set_attach_sampler(stage->globalDescSet, bindIndex, image, samplerSpec);

  stage->globalBoundMask |= 1 << bindIndex;
  stage->globalImages[imageIndex] = image;
}

void rvk_pass_stage_global_image(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  rvk_pass_stage_global_image_internal(pass, image, imageIndex, (RvkSamplerSpec){0});
}

void rvk_pass_stage_global_shadow(RvkPass* pass, RvkImage* image, const u16 imageIndex) {
  rvk_pass_stage_global_image_internal(
      pass,
      image,
      imageIndex,
      (RvkSamplerSpec){
          .flags = RvkSamplerFlags_SupportCompare, // Enable support for sampler2DShadow.
          .wrap  = RvkSamplerWrap_Zero,
      });
}

void rvk_pass_stage_draw_image(MAYBE_UNUSED RvkPass* pass, RvkImage* image) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");
  diag_assert_msg(image->caps & RvkImageCapability_Sampled, "Image does not support sampling");

  RvkPassStage* stage = rvk_pass_stage();
  for (u32 i = 0; i != pass_draw_image_max; ++i) {
    if (stage->drawImages[i] == image) {
      return; // Image was already staged.
    }
    if (!stage->drawImages[i]) {
      stage->drawImages[i] = image;
      return; // Image is staged in a empty slot.
    }
  }
  diag_assert_fail("Amount of staged per-draw images exceeds the maximum");
}

void rvk_pass_begin(RvkPass* pass) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassStage* stage  = rvk_pass_stage();
  RvkPassInvoc* invoc  = rvk_pass_invoc_begin(pass);
  invoc->size          = stage->size;
  invoc->vkFrameBuffer = rvk_framebuffer_create(pass, stage);

#ifndef VOLO_FAST
  // Validate that all images we load have content loaded in them.
  rvk_pass_assert_image_contents(pass, stage);
#endif

  invoc->statsRecord = rvk_statrecorder_start(pass->statrecorder, pass->vkCmdBuf);

  invoc->timeRecBegin = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);
  rvk_debug_label_begin(
      pass->dev->debug, pass->vkCmdBuf, geo_color_blue, "pass_{}", fmt_text(pass->config.name));

  /**
   * Execute image transitions:
   * - Attachment images to color/depth-attachment-optimal.
   * - Global images to ShaderRead.
   * - Per-draw images to ShaderRead.
   */
  {
    RvkImageTransition transitions[16];
    u32                transitionCount = 0;
    for (u32 i = 0; i != rvk_pass_attach_color_count(&pass->config); ++i) {
      transitions[transitionCount++] = (RvkImageTransition){
          .img   = stage->attachColors[i],
          .phase = RvkImagePhase_ColorAttachment,
      };
    }
    if (pass->config.attachDepth) {
      transitions[transitionCount++] = (RvkImageTransition){
          .img   = stage->attachDepth,
          .phase = RvkImagePhase_DepthAttachment,
      };
    }
    for (u32 i = 0; i != pass_global_image_max; ++i) {
      if (stage->globalImages[i]) {
        transitions[transitionCount++] = (RvkImageTransition){
            .img   = stage->globalImages[i],
            .phase = RvkImagePhase_ShaderRead,
        };
      }
    }
    for (u32 i = 0; i != pass_draw_image_max; ++i) {
      if (stage->drawImages[i]) {
        transitions[transitionCount++] = (RvkImageTransition){
            .img   = stage->drawImages[i],
            .phase = RvkImagePhase_ShaderRead,
        };
      }
    }
    rvk_image_transition_batch(transitions, transitionCount, pass->vkCmdBuf);
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
  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(invoc, "Pass invocation not active");

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
  if (UNLIKELY(graphic->drawDescMeta.bindings[0] && !draw->drawData.size)) {
    log_e("Graphic requires draw data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->drawDescMeta.bindings[1] && !draw->drawMesh)) {
    log_e("Graphic requires a draw-mesh", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->drawDescMeta.bindings[2] && !draw->drawImage)) {
    log_e("Graphic requires a draw-image", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireInstanceSet && !draw->instDataStride)) {
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

  ++invoc->drawCount;
  rvk_debug_label_begin(
      pass->dev->debug, pass->vkCmdBuf, geo_color_green, "draw_{}", fmt_text(graphic->dbgName));

  rvk_graphic_bind(graphic, pass->vkCmdBuf);

  if (graphic->flags & RvkGraphicFlags_RequireDrawSet) {
    rvk_pass_bind_draw(
        pass, stage, graphic, draw->drawData, draw->drawMesh, draw->drawImage, draw->drawSampler);
  }

  diag_assert(draw->instDataStride * draw->instCount == draw->instData.size);
  const u32 dataStride =
      graphic->flags & RvkGraphicFlags_RequireInstanceSet ? draw->instDataStride : 0;

  for (u32 remInstCount = draw->instCount, dataOffset = 0; remInstCount != 0;) {
    const u32 instCount = rvk_pass_instances_per_draw(pass, remInstCount, dataStride);
    invoc->instanceCount += instCount;

    if (dataStride) {
      const u32              dataSize   = instCount * dataStride;
      const Mem              data       = mem_slice(draw->instData, dataOffset, dataSize);
      const RvkUniformHandle dataHandle = rvk_uniform_upload(pass->uniformPool, data);
      rvk_uniform_dynamic_bind(
          pass->uniformPool,
          dataHandle,
          pass->vkCmdBuf,
          graphic->vkPipelineLayout,
          RvkGraphicSet_Instance);
      dataOffset += dataSize;
    }

    if (draw->drawMesh || graphic->mesh) {
      const u32 idxCount = draw->drawMesh ? draw->drawMesh->indexCount : graphic->mesh->indexCount;
      vkCmdDrawIndexed(pass->vkCmdBuf, idxCount, instCount, 0, 0, 0);
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

  rvk_statrecorder_stop(pass->statrecorder, invoc->statsRecord, pass->vkCmdBuf);
  vkCmdEndRenderPass(pass->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, pass->vkCmdBuf);
  invoc->timeRecEnd = rvk_stopwatch_mark(pass->stopwatch, pass->vkCmdBuf);

  if (stage->attachDepth && pass->config.attachDepth != RvkPassDepth_Stored) {
    // When we're not storing the depth, the image's contents become undefined.
    rvk_image_transition_external(stage->attachDepth, RvkImagePhase_Undefined);
  }

  *stage = (RvkPassStage){0}; // Reset the stage.
}

void rvk_pass_discard(RvkPass* pass) {
  RvkPassStage* stage = rvk_pass_stage();
  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(!invoc, "Pass cannot be active while discarding");
  (void)invoc;

  *stage = (RvkPassStage){0}; // Reset the stage.
}
