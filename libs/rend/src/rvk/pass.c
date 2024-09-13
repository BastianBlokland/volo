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
#include "job_internal.h"
#include "mesh_internal.h"
#include "pass_internal.h"
#include "repository_internal.h"
#include "statrecorder_internal.h"
#include "stopwatch_internal.h"
#include "texture_internal.h"
#include "uniform_internal.h"

#define pass_instance_count_max 2048
#define pass_attachment_max (rvk_pass_attach_color_max + 1)

typedef enum {
  RvkPassFlags_Active     = 1 << 0,
  RvkPassFlags_NeedsClear = 1 << 1,
} RvkPassFlags;

typedef struct {
  VkFramebuffer vkFrameBuffer;

  RvkSize size;
  u16     drawCount;
  u16     globalBoundMask; // Bitset of the bound global resources.
  u32     instanceCount;

  RvkStatRecord      statsRecord;
  RvkStopwatchRecord timeRecBegin, timeRecEnd;
} RvkPassInvoc;

static VkClearColorValue rvk_rend_clear_color(const GeoColor color) {
  VkClearColorValue result;
  mem_cpy(mem_var(result), mem_var(color));
  return result;
}

typedef enum {
  RvkPassFrameState_Available,
  RvkPassFrameState_Active,
  RvkPassFrameState_Reserved, // Waiting to be released.
} RvkPassFrameState;

typedef struct {
  RvkPassFrameState state;

  RvkUniformPool*  uniformPool;
  RvkStopwatch*    stopwatch;
  RvkStatRecorder* statrecorder;
  VkCommandBuffer  vkCmdBuf;

  DynArray descSetsVolatile; // RvkDescSet[], allocated on-demand and automatically freed next init.
  DynArray invocations;      // RvkPassInvoc[]
} RvkPassFrame;

struct sRvkPass {
  RvkDevice*           dev;
  const RvkPassConfig* config; // Persistently allocated.
  VkRenderPass         vkRendPass;
  RvkPassFlags         flags;
  RvkDescMeta          globalDescMeta;
  VkPipelineLayout     globalPipelineLayout;

  DynArray frames; // RvkPassFrame[]
};

static VkFormat rvk_attach_color_format(const RvkPass* pass, const u32 index) {
  diag_assert(index < rvk_pass_attach_color_max);
  const RvkPassFormat format = pass->config->attachColorFormat[index];
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
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentColor,
      "Pass {} color attachment {} invalid: Missing AttachmentColor capability",
      fmt_text(pass->config->name),
      fmt_int(idx));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} color attachment {} invalid: Missing capabilities",
      fmt_text(pass->config->name),
      fmt_int(idx));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} color attachment {} invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->config->name),
      fmt_int(idx),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
}

static void rvk_pass_attach_assert_depth(const RvkPass* pass, const RvkImage* img) {
  const RvkAttachSpec spec = rvk_pass_spec_attach_depth(pass);
  diag_assert_msg(
      img->caps & RvkImageCapability_AttachmentDepth,
      "Pass {} depth attachment invalid: Missing AttachmentDepth capability",
      fmt_text(pass->config->name));
  diag_assert_msg(
      (img->caps & spec.capabilities) == spec.capabilities,
      "Pass {} depth attachment invalid: Missing capabilities",
      fmt_text(pass->config->name));
  diag_assert_msg(
      img->vkFormat == spec.vkFormat,
      "Pass {} depth attachment invalid: Invalid format (expected: {}, actual: {})",
      fmt_text(pass->config->name),
      fmt_text(rvk_format_info(spec.vkFormat).name),
      fmt_text(rvk_format_info(img->vkFormat).name));
}

static void rvk_pass_assert_image_contents(const RvkPass* pass, const RvkPassSetup* setup) {
  // Validate preserved color attachment contents.
  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    if (pass->config->attachColorLoad[i] == RvkPassLoad_Preserve) {
      diag_assert_msg(
          setup->attachColors[i]->phase,
          "Pass {} preserved color attachment {} has undefined contents",
          fmt_text(pass->config->name),
          fmt_int(i));
    }
  }
  // Validate preserved depth attachment contents.
  if (pass->config->attachDepthLoad == RvkPassLoad_Preserve) {
    diag_assert_msg(
        setup->attachDepth->phase,
        "Pass {} preserved depth attachment has undefined contents",
        fmt_text(pass->config->name));
  }
  // Validate global image contents.
  for (u32 i = 0; i != rvk_pass_global_image_max; ++i) {
    if (setup->globalImages[i]) {
      diag_assert_msg(
          setup->globalImages[i]->phase,
          "Pass {} global image {} has undefined contents",
          fmt_text(pass->config->name),
          fmt_int(i));
    }
  }
}
#endif // !VOLO_FAST

static VkAttachmentLoadOp rvk_pass_attach_color_load_op(const RvkPass* pass, const u32 idx) {
  switch (pass->config->attachColorLoad[idx]) {
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
  switch (pass->config->attachDepthLoad) {
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
  return pass->config->attachDepth == RvkPassDepth_Stored ? VK_ATTACHMENT_STORE_OP_STORE
                                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkRenderPass rvk_renderpass_create(const RvkPass* pass) {
  VkAttachmentDescription attachments[pass_attachment_max];
  u32                     attachmentCount = 0;
  VkAttachmentReference   colorRefs[rvk_pass_attach_color_max];
  VkAttachmentReference   depthRef;
  bool                    hasDepthRef = false;

  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    if (pass->config->attachColorFormat[i] == RvkPassFormat_None) {
      continue; // Attachment binding unused.
    }
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

  if (pass->config->attachDepth) {
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
      .colorAttachmentCount    = rvk_pass_attach_color_count(pass->config),
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
  for (u16 globalDataIdx = 0; globalDataIdx != rvk_pass_global_data_max; ++globalDataIdx) {
    meta.bindings[globalBindingCount++] = RvkDescKind_UniformBuffer;
  }
  for (u16 globalImgIdx = 0; globalImgIdx != rvk_pass_global_image_max; ++globalImgIdx) {
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

static VkFramebuffer
rvk_framebuffer_create(RvkPass* pass, const RvkPassSetup* setup, const RvkSize size) {
  VkImageView attachments[pass_attachment_max];
  u32         attachCount = 0;
  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    if (pass->config->attachColorFormat[i] == RvkPassFormat_None) {
      continue; // Attachment binding unused.
    }
    diag_assert_msg(
        setup->attachColors[i],
        "Pass {} is missing color attachment {}",
        fmt_text(pass->config->name),
        fmt_int(i));
#ifndef VOLO_FAST
    rvk_pass_attach_assert_color(pass, i, setup->attachColors[i]);
#endif
    attachments[attachCount++] = setup->attachColors[i]->vkImageView;
  }
  if (pass->config->attachDepth) {
    diag_assert_msg(
        setup->attachDepth, "Pass {} is missing a depth attachment", fmt_text(pass->config->name));
#ifndef VOLO_FAST
    rvk_pass_attach_assert_depth(pass, setup->attachDepth);
#endif
    attachments[attachCount++] = setup->attachDepth->vkImageView;
  }

  const VkFramebufferCreateInfo framebufferInfo = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = pass->vkRendPass,
      .attachmentCount = attachCount,
      .pAttachments    = attachments,
      .width           = size.width,
      .height          = size.height,
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
    RvkPass* pass, RvkPassFrame* frame, RvkPassInvoc* invoc, const RvkPassSetup* setup) {

  VkClearValue clearValues[pass_attachment_max];
  u32          clearValueCount = 0;

  if (pass->flags & RvkPassFlags_NeedsClear) {
    for (u32 i = 0; i != rvk_pass_attach_color_count(pass->config); ++i) {
      clearValues[clearValueCount++].color = rvk_rend_clear_color(setup->clearColor);
    }
    if (pass->config->attachDepth) {
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
  vkCmdBeginRenderPass(frame->vkCmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

static RvkDescSet
rvk_pass_alloc_desc_volatile(RvkPass* pass, RvkPassFrame* frame, const RvkDescMeta* meta) {
  const RvkDescSet res = rvk_desc_alloc(pass->dev->descPool, meta);
  *dynarray_push_t(&frame->descSetsVolatile, RvkDescSet) = res;
  return res;
}

static void rvk_pass_bind_global(
    RvkPass* pass, RvkPassFrame* frame, RvkPassInvoc* invoc, const RvkPassSetup* setup) {
  diag_assert(!invoc->globalBoundMask);

  RvkDescSet globalDescSet;
  u32        binding = 0;

  // Attach global data.
  for (; binding != rvk_pass_global_data_max; ++binding) {
    const RvkUniformHandle data = setup->globalData[binding];
    if (!data) {
      continue; // Global data binding unused.
    }
    if (!invoc->globalBoundMask) {
      globalDescSet = rvk_pass_alloc_desc_volatile(pass, frame, &pass->globalDescMeta);
    }
    diag_assert(!rvk_uniform_next(frame->uniformPool, data));
    rvk_uniform_attach(frame->uniformPool, data, globalDescSet, binding);
    invoc->globalBoundMask |= 1 << binding;
  }

  // Attach global images.
  for (u32 i = 0; i != rvk_pass_global_image_max; ++i, ++binding) {
    RvkImage* img = setup->globalImages[i];
    if (!img) {
      continue; // Global image binding unused.
    }
    if (!invoc->globalBoundMask) {
      globalDescSet = rvk_pass_alloc_desc_volatile(pass, frame, &pass->globalDescMeta);
    }

    if (UNLIKELY(img->type == RvkImageType_ColorSourceCube)) {
      log_e("Cube images cannot be bound globally");
      const RvkRepositoryId missing = RvkRepositoryId_MissingTexture;
      // TODO: This cast violates const-correctness.
      img = (RvkImage*)&rvk_repository_texture_get(pass->dev->repository, missing)->image;
    }

    diag_assert_msg(img->caps & RvkImageCapability_Sampled, "Image does not support sampling");
    rvk_desc_set_attach_sampler(globalDescSet, binding, img, setup->globalImageSamplers[i]);

    invoc->globalBoundMask |= 1 << binding;
  }

  if (invoc->globalBoundMask) {
    const VkDescriptorSet vkDescSets[] = {rvk_desc_set_vkset(globalDescSet)};
    vkCmdBindDescriptorSets(
        frame->vkCmdBuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pass->globalPipelineLayout,
        RvkGraphicSet_Global,
        array_elems(vkDescSets),
        vkDescSets,
        0,
        null);
  }
}

static void rvk_pass_bind_draw(
    RvkPass*           pass,
    RvkPassFrame*      frame,
    MAYBE_UNUSED const RvkPassSetup* setup,
    const RvkGraphic*                gra,
    const RvkUniformHandle           data,
    const RvkMesh*                   mesh,
    RvkImage*                        img,
    const RvkSamplerSpec             sampler) {
  diag_assert_msg(!mesh || rvk_mesh_is_ready(mesh, pass->dev), "Mesh is not ready for binding");
  diag_assert_msg(!img || img->phase != RvkImagePhase_Undefined, "Image has no content");
  diag_assert_msg(!img || img->caps & RvkImageCapability_Sampled, "Image doesn't support sampling");

  const RvkDescSet descSet = rvk_pass_alloc_desc_volatile(pass, frame, &gra->drawDescMeta);
  if (data && gra->drawDescMeta.bindings[0]) {
    diag_assert(!rvk_uniform_next(frame->uniformPool, data));
    rvk_uniform_attach(frame->uniformPool, data, descSet, 0 /* binding */);
  }
  if (mesh && gra->drawDescMeta.bindings[1]) {
    rvk_desc_set_attach_buffer(descSet, 1 /* binding */, &mesh->vertexBuffer, 0, 0);
  }
  if (img && gra->drawDescMeta.bindings[2]) {
    const bool reqCube = gra->drawDescMeta.bindings[2] == RvkDescKind_CombinedImageSamplerCube;
    if (UNLIKELY(reqCube != (img->type == RvkImageType_ColorSourceCube))) {
      log_e("Unsupported draw image type", log_param("graphic", fmt_text(gra->dbgName)));

      const RvkRepositoryId missing =
          reqCube ? RvkRepositoryId_MissingTextureCube : RvkRepositoryId_MissingTexture;
      // TODO: This cast violates const-correctness.
      img = (RvkImage*)&rvk_repository_texture_get(pass->dev->repository, missing)->image;
    }
    rvk_desc_set_attach_sampler(descSet, 2, img, sampler);
  }

  const VkDescriptorSet vkDescSets[] = {rvk_desc_set_vkset(descSet)};
  vkCmdBindDescriptorSets(
      frame->vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      gra->vkPipelineLayout,
      RvkGraphicSet_Draw,
      array_elems(vkDescSets),
      vkDescSets,
      0,
      null);

  if (mesh) {
    rvk_mesh_bind(mesh, pass->dev, frame->vkCmdBuf);
  }
}

static RvkPassFrame* rvk_pass_frame_get_active(RvkPass* pass) {
  dynarray_for_t(&pass->frames, RvkPassFrame, frame) {
    if (frame->state == RvkPassFrameState_Active) {
      return frame;
    }
  }
  return null;
}

static const RvkPassFrame* rvk_pass_frame_get(const RvkPass* pass, const RvkPassHandle handle) {
  diag_assert(handle < pass->frames.size);
  return &dynarray_begin_t(&pass->frames, RvkPassFrame)[handle];
}

static RvkPassFrame* rvk_pass_frame_get_mut(RvkPass* pass, const RvkPassHandle handle) {
  diag_assert(handle < pass->frames.size);
  return &dynarray_begin_t(&pass->frames, RvkPassFrame)[handle];
}

static RvkPassHandle rvk_pass_frame_find_available(RvkPass* pass) {
  for (RvkPassHandle handle = 0; handle != pass->frames.size; ++handle) {
    if (rvk_pass_frame_get(pass, handle)->state == RvkPassFrameState_Available) {
      return handle;
    }
  }
  return sentinel_u8;
}

static RvkPassHandle rvk_pass_frame_create(RvkPass* pass) {
  const RvkPassHandle frameHandle = (RvkPassHandle)pass->frames.size;
  RvkPassFrame*       frame       = dynarray_push_t(&pass->frames, RvkPassFrame);

  *frame = (RvkPassFrame){
      .state            = RvkPassFrameState_Available,
      .descSetsVolatile = dynarray_create_t(g_allocHeap, RvkDescSet, 8),
      .invocations      = dynarray_create_t(g_allocHeap, RvkPassInvoc, 1),
  };

  return frameHandle;
}

static void rvk_pass_frame_reset(RvkPass* pass, RvkPassFrame* frame) {
  diag_assert(frame->state == RvkPassFrameState_Reserved);

  // Cleanup invocations.
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) {
    vkDestroyFramebuffer(pass->dev->vkDev, invoc->vkFrameBuffer, &pass->dev->vkAlloc);
  }
  dynarray_clear(&frame->invocations);

  // Cleanup volatile descriptor sets.
  dynarray_for_t(&frame->descSetsVolatile, RvkDescSet, set) { rvk_desc_free(*set); }
  dynarray_clear(&frame->descSetsVolatile);

  frame->state = RvkPassFrameState_Available;
}

static void rvk_pass_frame_destroy(RvkPass* pass, RvkPassFrame* frame) {
  // Cleanup invocations.
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) {
    vkDestroyFramebuffer(pass->dev->vkDev, invoc->vkFrameBuffer, &pass->dev->vkAlloc);
  }
  dynarray_destroy(&frame->invocations);

  // Cleanup volatile descriptor sets.
  dynarray_for_t(&frame->descSetsVolatile, RvkDescSet, set) { rvk_desc_free(*set); }
  dynarray_destroy(&frame->descSetsVolatile);
}

static RvkPassInvoc* rvk_pass_invoc_begin(RvkPass* pass, RvkPassFrame* frame) {
  pass->flags |= RvkPassFlags_Active;
  RvkPassInvoc* res = dynarray_push_t(&frame->invocations, RvkPassInvoc);
  *res              = (RvkPassInvoc){0};
  return res;
}

static RvkPassInvoc* rvk_pass_invoc_active(RvkPass* pass) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);
  if (!frame || !(pass->flags & RvkPassFlags_Active)) {
    return null;
  }
  return dynarray_at_t(&frame->invocations, frame->invocations.size - 1, RvkPassInvoc);
}

static RvkSize rvk_pass_size(MAYBE_UNUSED const RvkPass* pass, const RvkPassSetup* setup) {
  RvkSize result = {0};
  if (setup->attachDepth) {
    result = setup->attachDepth->size;
  }
  for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
    const RvkImage* img = setup->attachColors[i];
    if (!img) {
      continue; // Attachment binding unused.
    }
    if (!result.data) {
      result = img->size;
    } else {
      diag_assert_msg(
          img->size.data == result.data,
          "Pass {} color attachment {} invalid: Invalid size (expected: {}x{}, actual: {}x{})",
          fmt_text(pass->config->name),
          fmt_int(i),
          fmt_int(result.width),
          fmt_int(result.height),
          fmt_int(img->size.width),
          fmt_int(img->size.height));
    }
  }
  return result;
}

RvkPass* rvk_pass_create(RvkDevice* dev, const RvkPassConfig* config) {
  diag_assert(!string_is_empty(config->name));

  RvkPass* pass = alloc_alloc_t(g_allocHeap, RvkPass);

  *pass = (RvkPass){
      .dev    = dev,
      .config = config,
      .frames = dynarray_create_t(g_allocHeap, RvkPassFrame, 2),
  };

  pass->vkRendPass = rvk_renderpass_create(pass);
  rvk_debug_name_pass(dev->debug, pass->vkRendPass, "{}", fmt_text(config->name));

  pass->globalDescMeta       = rvk_global_desc_meta();
  pass->globalPipelineLayout = rvk_global_layout_create(dev, &pass->globalDescMeta);

  bool anyAttachmentNeedsClear = pass->config->attachDepthLoad == RvkPassLoad_Clear;
  array_for_t(pass->config->attachColorLoad, RvkPassLoad, load) {
    anyAttachmentNeedsClear |= *load == RvkPassLoad_Clear;
  }
  if (anyAttachmentNeedsClear) {
    pass->flags |= RvkPassFlags_NeedsClear;
  }

  return pass;
}

void rvk_pass_destroy(RvkPass* pass) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation still active");

  dynarray_for_t(&pass->frames, RvkPassFrame, frame) { rvk_pass_frame_destroy(pass, frame); }
  dynarray_destroy(&pass->frames);

  vkDestroyRenderPass(pass->dev->vkDev, pass->vkRendPass, &pass->dev->vkAlloc);
  vkDestroyPipelineLayout(pass->dev->vkDev, pass->globalPipelineLayout, &pass->dev->vkAlloc);

  alloc_free_t(g_allocHeap, pass);
}

const RvkPassConfig* rvk_pass_config(const RvkPass* pass) { return pass->config; }

bool rvk_pass_active(const RvkPass* pass) { return rvk_pass_invoc_active((RvkPass*)pass) != null; }

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass* pass, const u16 colorAttachIndex) {
  RvkImageCapability capabilities = 0;

  // TODO: Specifying these capabilities should not be the responsibilty of the pass.
  capabilities |= RvkImageCapability_TransferSource | RvkImageCapability_Sampled;

  return (RvkAttachSpec){
      .vkFormat     = rvk_attach_color_format(pass, colorAttachIndex),
      .capabilities = capabilities,
  };
}

RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass* pass) {
  RvkImageCapability capabilities = 0;
  if (pass->config->attachDepth == RvkPassDepth_Stored) {
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

RvkPassHandle rvk_pass_frame_begin(RvkPass* pass, RvkJob* job) {
  diag_assert_msg(!rvk_pass_frame_get_active(pass), "Pass frame already active");
  diag_assert_msg(pass->frames.size <= u8_max, "Pass frame limit exceeded");

  RvkPassHandle frameHandle = rvk_pass_frame_find_available(pass);
  if (sentinel_check(frameHandle)) {
    frameHandle = rvk_pass_frame_create(pass);
  }

  RvkPassFrame* frame = rvk_pass_frame_get_mut(pass, frameHandle);
  frame->state        = RvkPassFrameState_Active;
  frame->uniformPool  = rvk_job_uniform_pool(job);
  frame->stopwatch    = rvk_job_stopwatch(job);
  frame->statrecorder = rvk_job_statrecorder(job);
  frame->vkCmdBuf     = rvk_job_drawbuffer(job);

  return frameHandle;
}

void rvk_pass_frame_end(RvkPass* pass, const RvkPassHandle frameHandle) {
  diag_assert_msg(!rvk_pass_invoc_active((RvkPass*)pass), "Pass invocation still active");

  RvkPassFrame* frame = rvk_pass_frame_get_mut(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Active, "Pass frame not active");

  frame->state       = RvkPassFrameState_Reserved;
  frame->vkCmdBuf    = null; // No more commands should be submitted to this frame.
  frame->uniformPool = null; // NO more data should be allocated as part of this frame.
}

void rvk_pass_frame_release(RvkPass* pass, const RvkPassHandle frameHandle) {
  RvkPassFrame* frame = rvk_pass_frame_get_mut(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame still active");

  rvk_pass_frame_reset(pass, frame);
}

u16 rvk_pass_stat_invocations(const RvkPass* pass, const RvkPassHandle frameHandle) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  return (u16)dynarray_size(&frame->invocations);
}

u16 rvk_pass_stat_draws(const RvkPass* pass, const RvkPassHandle frameHandle) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  u16 draws = 0;
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) { draws += invoc->drawCount; }
  return draws;
}

u32 rvk_pass_stat_instances(const RvkPass* pass, const RvkPassHandle frameHandle) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  u32 draws = 0;
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) { draws += invoc->instanceCount; }
  return draws;
}

RvkSize rvk_pass_stat_size_max(const RvkPass* pass, const RvkPassHandle frameHandle) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  RvkSize size = {0};
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) {
    size.width  = math_max(size.width, invoc->size.width);
    size.height = math_max(size.height, invoc->size.height);
  }
  return size;
}

TimeDuration rvk_pass_stat_duration(const RvkPass* pass, const RvkPassHandle frameHandle) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  TimeDuration dur = 0;
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) {
    const TimeSteady timestampBegin = rvk_stopwatch_query(frame->stopwatch, invoc->timeRecBegin);
    const TimeSteady timestampEnd   = rvk_stopwatch_query(frame->stopwatch, invoc->timeRecEnd);
    dur += time_steady_duration(timestampBegin, timestampEnd);
  }
  return dur;
}

u64 rvk_pass_stat_pipeline(
    const RvkPass* pass, const RvkPassHandle frameHandle, const RvkStat stat) {
  const RvkPassFrame* frame = rvk_pass_frame_get(pass, frameHandle);
  diag_assert_msg(frame->state == RvkPassFrameState_Reserved, "Pass frame already released");

  u64 res = 0;
  dynarray_for_t(&frame->invocations, RvkPassInvoc, invoc) {
    res += rvk_statrecorder_query(frame->statrecorder, invoc->statsRecord, stat);
  }
  return res;
}

u32 rvk_pass_batch_size(RvkPass* pass, const u32 instanceDataSize) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);
  if (!instanceDataSize) {
    return pass_instance_count_max;
  }
  const u32 uniformMaxInstances = rvk_uniform_size_max(frame->uniformPool) / instanceDataSize;
  return math_min(uniformMaxInstances, pass_instance_count_max);
}

RvkUniformHandle rvk_pass_uniform_upload(RvkPass* pass, const Mem data) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);
  return rvk_uniform_upload(frame->uniformPool, data);
}

RvkUniformHandle
rvk_pass_uniform_upload_next(RvkPass* pass, const RvkUniformHandle prev, const Mem data) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);
  return rvk_uniform_upload_next(frame->uniformPool, prev, data);
}

void rvk_pass_begin(RvkPass* pass, const RvkPassSetup* setup) {
  diag_assert_msg(!rvk_pass_invoc_active(pass), "Pass invocation already active");

  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);

  RvkPassInvoc* invoc  = rvk_pass_invoc_begin(pass, frame);
  invoc->size          = rvk_pass_size(pass, setup);
  invoc->vkFrameBuffer = rvk_framebuffer_create(pass, setup, invoc->size);

#ifndef VOLO_FAST
  // Validate that all images we load have content loaded in them.
  rvk_pass_assert_image_contents(pass, setup);
#endif

  invoc->statsRecord = rvk_statrecorder_start(frame->statrecorder, frame->vkCmdBuf);

  invoc->timeRecBegin = rvk_stopwatch_mark(frame->stopwatch, frame->vkCmdBuf);
  rvk_debug_label_begin(
      pass->dev->debug, frame->vkCmdBuf, geo_color_blue, "pass_{}", fmt_text(pass->config->name));

  /**
   * Execute image transitions:
   * - Attachment images to color/depth-attachment-optimal.
   * - Global images to ShaderRead.
   * - Per-draw images to ShaderRead.
   */
  {
    RvkImageTransition transitions[16];
    u32                transitionCount = 0;
    for (u32 i = 0; i != rvk_pass_attach_color_max; ++i) {
      if (!setup->attachColors[i]) {
        continue; // Color attachment binding unused.
      }
      transitions[transitionCount++] = (RvkImageTransition){
          .img   = setup->attachColors[i],
          .phase = RvkImagePhase_ColorAttachment,
      };
    }
    if (pass->config->attachDepth) {
      transitions[transitionCount++] = (RvkImageTransition){
          .img   = setup->attachDepth,
          .phase = RvkImagePhase_DepthAttachment,
      };
    }
    for (u32 i = 0; i != rvk_pass_global_image_max; ++i) {
      if (setup->globalImages[i]) {
        transitions[transitionCount++] = (RvkImageTransition){
            .img   = setup->globalImages[i],
            .phase = RvkImagePhase_ShaderRead,
        };
      }
    }
    for (u32 i = 0; i != rvk_pass_draw_image_max; ++i) {
      if (setup->drawImages[i]) {
        transitions[transitionCount++] = (RvkImageTransition){
            .img   = setup->drawImages[i],
            .phase = RvkImagePhase_ShaderRead,
        };
      }
    }
    rvk_image_transition_batch(transitions, transitionCount, frame->vkCmdBuf);
  }

  rvk_pass_vkrenderpass_begin(pass, frame, invoc, setup);

  rvk_pass_viewport_set(frame->vkCmdBuf, invoc->size);
  rvk_pass_scissor_set(frame->vkCmdBuf, invoc->size);

  rvk_pass_bind_global(pass, frame, invoc, setup);
}

void rvk_pass_draw(RvkPass* pass, const RvkPassSetup* setup, const RvkPassDraw* draw) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);

  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(invoc, "Pass invocation not active");

  RvkImage* drawImg = null;
  if (!sentinel_check(draw->drawImageIndex)) {
    diag_assert(draw->drawImageIndex < rvk_pass_draw_image_max);
    drawImg = setup->drawImages[draw->drawImageIndex];
  }

  const RvkGraphic* graphic = draw->graphic;
  if (UNLIKELY((graphic->globalBindings & invoc->globalBoundMask) != graphic->globalBindings)) {
    log_e(
        "Graphic requires additional global bindings",
        log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->drawDescMeta.bindings[0] && !draw->drawData)) {
    log_e("Graphic requires draw data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->drawDescMeta.bindings[1] && !draw->drawMesh)) {
    log_e("Graphic requires a draw-mesh", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->drawDescMeta.bindings[2] && !drawImg)) {
    log_e("Graphic requires a draw-image", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_RequireInstanceSet && !draw->instDataStride)) {
    log_e("Graphic requires instance data", log_param("graphic", fmt_text(graphic->dbgName)));
    return;
  }
  if (UNLIKELY(draw->instDataStride > rvk_uniform_size_max(frame->uniformPool))) {
    log_e(
        "Draw instance data exceeds maximum",
        log_param("graphic", fmt_text(graphic->dbgName)),
        log_param("size", fmt_size(draw->instDataStride)),
        log_param("size-max", fmt_size(rvk_uniform_size_max(frame->uniformPool))));
    return;
  }

  ++invoc->drawCount;
  rvk_debug_label_begin(
      pass->dev->debug, frame->vkCmdBuf, geo_color_green, "draw_{}", fmt_text(graphic->dbgName));

  rvk_graphic_bind(graphic, pass->dev, pass, frame->vkCmdBuf);

  if (graphic->flags & RvkGraphicFlags_RequireDrawSet) {
    rvk_pass_bind_draw(
        pass, frame, setup, graphic, draw->drawData, draw->drawMesh, drawImg, draw->drawSampler);
  }

  const bool instReqData   = (graphic->flags & RvkGraphicFlags_RequireInstanceSet) != 0;
  const u32  instBatchSize = rvk_pass_batch_size(pass, instReqData ? draw->instDataStride : 0);
  RvkUniformHandle instBatchData = draw->instData;

  for (u32 remInstCount = draw->instCount; remInstCount != 0;) {
    const u32 instCount = math_min(remInstCount, instBatchSize);

    if (instReqData) {
      diag_assert(
          rvk_uniform_size(frame->uniformPool, instBatchData) == instCount * draw->instDataStride);
      rvk_uniform_dynamic_bind(
          frame->uniformPool,
          instBatchData,
          frame->vkCmdBuf,
          graphic->vkPipelineLayout,
          RvkGraphicSet_Instance);
      instBatchData = rvk_uniform_next(frame->uniformPool, instBatchData);
    }

    if (draw->drawMesh || graphic->mesh) {
      const u32 idxCount = draw->drawMesh ? draw->drawMesh->indexCount : graphic->mesh->indexCount;
      vkCmdDrawIndexed(frame->vkCmdBuf, idxCount, instCount, 0, 0, 0);
    } else {
      const u32 vertexCount =
          draw->vertexCountOverride ? draw->vertexCountOverride : graphic->vertexCount;
      if (LIKELY(vertexCount)) {
        vkCmdDraw(frame->vkCmdBuf, vertexCount, instCount, 0, 0);
      }
    }

    invoc->instanceCount += instCount;
    remInstCount -= instCount;
  }

  rvk_debug_label_end(pass->dev->debug, frame->vkCmdBuf);
}

void rvk_pass_end(RvkPass* pass, const RvkPassSetup* setup) {
  RvkPassFrame* frame = rvk_pass_frame_get_active(pass);

  RvkPassInvoc* invoc = rvk_pass_invoc_active(pass);
  diag_assert_msg(invoc, "Pass not active");

  pass->flags &= ~RvkPassFlags_Active;

  rvk_statrecorder_stop(frame->statrecorder, invoc->statsRecord, frame->vkCmdBuf);
  vkCmdEndRenderPass(frame->vkCmdBuf);

  rvk_debug_label_end(pass->dev->debug, frame->vkCmdBuf);
  invoc->timeRecEnd = rvk_stopwatch_mark(frame->stopwatch, frame->vkCmdBuf);

  if (setup->attachDepth && pass->config->attachDepth != RvkPassDepth_Stored) {
    // When we're not storing the depth, the image's contents become undefined.
    rvk_image_transition_external(setup->attachDepth, RvkImagePhase_Undefined);
  }
}
