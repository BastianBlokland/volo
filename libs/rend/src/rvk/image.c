#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#include "device_internal.h"
#include "image_internal.h"

#include <vulkan/vulkan_core.h>

static VkClearColorValue rvk_rend_clear_color(const GeoColor color) {
  VkClearColorValue result;
  mem_cpy(mem_var(result), mem_var(color));
  return result;
}

MAYBE_UNUSED static bool
rvk_image_phase_supported(const RvkImageCapability caps, const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    return true;
  case RvkImagePhase_TransferSource:
    return (caps & RvkImageCapability_TransferSource) != 0;
  case RvkImagePhase_TransferDest:
    return (caps & RvkImageCapability_TransferDest) != 0;
  case RvkImagePhase_ColorAttachment:
    return (caps & RvkImageCapability_AttachmentColor) != 0;
  case RvkImagePhase_DepthAttachment:
    return (caps & RvkImageCapability_AttachmentDepth) != 0;
  case RvkImagePhase_ShaderRead:
    return (caps & RvkImageCapability_Sampled) != 0;
  case RvkImagePhase_Present:
    return (caps & RvkImageCapability_Present) != 0;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkAccessFlags rvk_image_vkaccess_read(const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    return 0;
  case RvkImagePhase_TransferSource:
    return VK_ACCESS_TRANSFER_READ_BIT;
  case RvkImagePhase_TransferDest:
    return 0;
  case RvkImagePhase_ColorAttachment:
    return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  case RvkImagePhase_DepthAttachment:
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  case RvkImagePhase_ShaderRead:
    return VK_ACCESS_SHADER_READ_BIT;
  case RvkImagePhase_Present:
    return 0;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkAccessFlags rvk_image_vkaccess_write(const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    /**
     * For images in an undefined phase we have to assume they can be written to by any part of the
     * pipeline. This is needed to synchronize write-after-write scenarios when reusing memory.
     * TODO: Investigate the performance impact of this and consider adding additional bookkeeping
     * to narrow this down.
     */
    return VK_ACCESS_MEMORY_WRITE_BIT;
  case RvkImagePhase_TransferSource:
    return 0;
  case RvkImagePhase_TransferDest:
    return VK_ACCESS_TRANSFER_WRITE_BIT;
  case RvkImagePhase_ColorAttachment:
    return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  case RvkImagePhase_DepthAttachment:
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case RvkImagePhase_ShaderRead:
    return 0;
  case RvkImagePhase_Present:
    return 0;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkPipelineStageFlags rvk_image_vkpipelinestage(const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    /**
     * For images in an undefined phase we have to assume they are used in any part of the pipeline.
     * TODO: Investigate the performance impact of this and consider adding additional bookkeeping
     * to narrow this down.
     */
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  case RvkImagePhase_TransferSource:
  case RvkImagePhase_TransferDest:
    return VK_PIPELINE_STAGE_TRANSFER_BIT;
  case RvkImagePhase_ColorAttachment:
    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  case RvkImagePhase_DepthAttachment:
    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  case RvkImagePhase_ShaderRead:
    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  case RvkImagePhase_Present:
    return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkImageLayout rvk_image_vklayout(const RvkImageType type, const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    return VK_IMAGE_LAYOUT_UNDEFINED;
  case RvkImagePhase_TransferSource:
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  case RvkImagePhase_TransferDest:
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  case RvkImagePhase_ColorAttachment:
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  case RvkImagePhase_DepthAttachment:
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  case RvkImagePhase_ShaderRead:
    return type == RvkImageType_DepthAttachment ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  case RvkImagePhase_Present:
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkImageAspectFlags rvk_image_vkaspect(const RvkImageType type) {
  switch (type) {
  case RvkImageType_ColorSource:
  case RvkImageType_ColorSourceCube:
  case RvkImageType_ColorAttachment:
  case RvkImageType_Swapchain:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  case RvkImageType_DepthAttachment:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  default:
    return 0;
  }
}

static VkImageUsageFlags rvk_image_vkusage(const RvkImageCapability caps) {
  VkImageUsageFlags usage = 0;
  if (caps & RvkImageCapability_TransferSource) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (caps & RvkImageCapability_TransferDest) {
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if (caps & RvkImageCapability_Sampled) {
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (caps & RvkImageCapability_AttachmentColor) {
    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (caps & RvkImageCapability_AttachmentDepth) {
    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  return usage;
}

static VkFormatFeatureFlags rvk_image_format_features(const RvkImageCapability caps) {
  VkFormatFeatureFlags formatFeatures = 0;
  if (caps & RvkImageCapability_TransferSource) {
    formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  }
  if (caps & RvkImageCapability_TransferDest) {
    formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  }
  if (caps & RvkImageCapability_BlitDest) {
    formatFeatures |= VK_FORMAT_FEATURE_BLIT_DST_BIT;
  }
  if (caps & RvkImageCapability_Sampled) {
    formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  }
  if (caps & RvkImageCapability_AttachmentColor) {
    formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  }
  if (caps & RvkImageCapability_AttachmentDepth) {
    formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  return formatFeatures;
}

static VkImageCreateFlags rvk_image_create_flags(const RvkImageType type, const u8 layers) {
  switch (type) {
  case RvkImageType_ColorSourceCube:
    return VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  default:
    return layers > 1 ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT : 0;
  }
}

static VkImageViewType rvk_image_viewtype(const RvkImageType type, const u8 layers) {
  switch (type) {
  case RvkImageType_ColorSourceCube:
    return layers > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
  default:
    return layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  }
}

static VkImageMemoryBarrier rvk_image_barrier(
    const RvkImage*     img,
    const u32           srcQueueFamIdx,
    const u32           dstQueueFamIdx,
    const VkImageLayout oldLayout,
    const VkImageLayout newLayout,
    const VkAccessFlags srcAccess,
    const VkAccessFlags dstAccess,
    const u8            baseMip,
    const u8            mipLevels) {

  return (VkImageMemoryBarrier){
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout                       = oldLayout,
      .newLayout                       = newLayout,
      .srcQueueFamilyIndex             = srcQueueFamIdx,
      .dstQueueFamilyIndex             = dstQueueFamIdx,
      .image                           = img->vkImage,
      .subresourceRange.aspectMask     = rvk_image_vkaspect(img->type),
      .subresourceRange.baseMipLevel   = baseMip,
      .subresourceRange.levelCount     = mipLevels,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount     = img->layers,
      .srcAccessMask                   = srcAccess,
      .dstAccessMask                   = dstAccess,
  };
}

static VkImageMemoryBarrier rvk_image_barrier_from_to(
    const RvkImage*     img,
    const RvkImagePhase from,
    const RvkImagePhase to,
    const u8            baseMip,
    const u8            mipLevels) {
  return rvk_image_barrier(
      img,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      rvk_image_vklayout(img->type, from),
      rvk_image_vklayout(img->type, to),
      rvk_image_vkaccess_write(from),
      rvk_image_vkaccess_read(to) | rvk_image_vkaccess_write(to),
      baseMip,
      mipLevels);
}

static VkImage rvk_vkimage_create(
    RvkDevice*              dev,
    const RvkImageType      type,
    const RvkSize           size,
    const VkFormat          vkFormat,
    const VkImageUsageFlags vkImgUsages,
    const u8                layers,
    const u8                mipLevels) {

  const VkImageCreateInfo imageInfo = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags         = rvk_image_create_flags(type, layers),
      .imageType     = VK_IMAGE_TYPE_2D,
      .extent.width  = size.width,
      .extent.height = size.height,
      .extent.depth  = 1,
      .mipLevels     = mipLevels,
      .arrayLayers   = layers,
      .format        = vkFormat,
      .tiling        = VK_IMAGE_TILING_OPTIMAL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .usage         = vkImgUsages,
      .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
      .samples       = 1,
  };
  VkImage result;
  rvk_call(vkCreateImage, dev->vkDev, &imageInfo, &dev->vkAlloc, &result);
  return result;
}

static VkImageView rvk_vkimageview_create(
    RvkDevice*               dev,
    const RvkImageType       type,
    const VkImage            vkImage,
    const VkFormat           vkFormat,
    const VkImageAspectFlags vkAspect,
    const u8                 layers,
    const u8                 mipLevels) {

  const VkImageViewCreateInfo createInfo = {
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image                           = vkImage,
      .viewType                        = rvk_image_viewtype(type, layers),
      .format                          = vkFormat,
      .subresourceRange.aspectMask     = vkAspect,
      .subresourceRange.baseMipLevel   = 0,
      .subresourceRange.levelCount     = mipLevels,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount     = layers,
  };
  VkImageView result;
  rvk_call(vkCreateImageView, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static RvkImage rvk_image_create_backed(
    RvkDevice*               dev,
    const RvkImageType       type,
    const RvkImageCapability caps,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const u8                 layers,
    const u8                 mipLevels) {
  diag_assert_msg(layers, "Image needs at least 1 layer");
  diag_assert_msg(mipLevels, "Image needs at least 1 mipmap");

  const VkFormatFeatureFlags vkFormatFeatures = rvk_image_format_features(caps);
  if (UNLIKELY(!rvk_device_format_supported(dev, vkFormat, vkFormatFeatures))) {
    diag_crash_msg(
        "Image format {} does not support requested features",
        fmt_text(rvk_format_info(vkFormat).name));
  }
  if (UNLIKELY(layers > dev->vkProperties.limits.maxImageArrayLayers)) {
    diag_crash_msg("Image layer count {} unsupported", fmt_int(layers));
  }

  const VkImageAspectFlags vkAspect = rvk_image_vkaspect(type);
  const VkImageUsageFlags  vkUsage  = rvk_image_vkusage(caps);
  const VkImage vkImage = rvk_vkimage_create(dev, type, size, vkFormat, vkUsage, layers, mipLevels);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(dev->vkDev, vkImage, &memReqs);

  const RvkMemLoc memLoc = RvkMemLoc_Dev;
  const RvkMem    mem    = rvk_mem_alloc_req(dev->memPool, memLoc, RvkMemAccess_NonLinear, memReqs);
  rvk_mem_bind_image(mem, vkImage);

  const VkImageView vkView =
      rvk_vkimageview_create(dev, type, vkImage, vkFormat, vkAspect, layers, mipLevels);

  return (RvkImage){
      .type        = type,
      .phase       = RvkImagePhase_Undefined,
      .caps        = caps,
      .vkFormat    = vkFormat,
      .size        = size,
      .layers      = layers,
      .mipLevels   = mipLevels,
      .vkImage     = vkImage,
      .vkImageView = vkView,
      .mem         = mem,
  };
}

RvkImage rvk_image_create_source_color(
    RvkDevice*     dev,
    const VkFormat vkFormat,
    const RvkSize  size,
    const u8       layers,
    const u8       mipLevels,
    const bool     mipGpuGen) {
  RvkImageCapability caps = RvkImageCapability_Sampled | RvkImageCapability_TransferDest;
  if (mipGpuGen && mipLevels > 1) {
    caps |= RvkImageCapability_TransferSource | RvkImageCapability_BlitDest;
  }
  return rvk_image_create_backed(
      dev, RvkImageType_ColorSource, caps, vkFormat, size, layers, mipLevels);
}

RvkImage rvk_image_create_source_color_cube(
    RvkDevice*     dev,
    const VkFormat vkFormat,
    const RvkSize  size,
    const u8       mipLevels,
    const bool     mipGpuGen) {
  RvkImageCapability caps = RvkImageCapability_Sampled | RvkImageCapability_TransferDest;
  if (mipGpuGen && mipLevels > 1) {
    caps |= RvkImageCapability_TransferSource | RvkImageCapability_BlitDest;
  }
  const u8 layers = 6;
  return rvk_image_create_backed(
      dev, RvkImageType_ColorSourceCube, caps, vkFormat, size, layers, mipLevels);
}

RvkImage rvk_image_create_attach_color(
    RvkDevice*               dev,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability extraCaps) {
  diag_assert((extraCaps & (RvkImageCapability_AttachmentDepth | RvkImageCapability_Present)) == 0);

  const RvkImageCapability caps      = RvkImageCapability_AttachmentColor | extraCaps;
  const u8                 layers    = 1;
  const u8                 mipLevels = 1;
  return rvk_image_create_backed(
      dev, RvkImageType_ColorAttachment, caps, vkFormat, size, layers, mipLevels);
}

RvkImage rvk_image_create_attach_depth(
    RvkDevice*               dev,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability extraCaps) {
  diag_assert(rvk_format_info(vkFormat).channels == 1);
  diag_assert((extraCaps & (RvkImageCapability_AttachmentColor | RvkImageCapability_Present)) == 0);

  const RvkImageCapability caps      = RvkImageCapability_AttachmentDepth | extraCaps;
  const u8                 layers    = 1;
  const u8                 mipLevels = 1;
  return rvk_image_create_backed(
      dev, RvkImageType_DepthAttachment, caps, vkFormat, size, layers, mipLevels);
}

RvkImage
rvk_image_create_swapchain(RvkDevice* dev, VkImage vkImage, VkFormat vkFormat, const RvkSize size) {
  RvkImageCapability capabilities = RvkImageCapability_Present;

  /**
   * Support both rendering into a swapchain image and blitting / copying into it.
   * TODO: Consider allowing the caller to specify how they want to populate the swapchain-image.
   */
  capabilities |= RvkImageCapability_AttachmentColor;
  capabilities |= RvkImageCapability_TransferDest;
  capabilities |= RvkImageCapability_BlitDest;

  const u8 layers    = 1;
  const u8 mipLevels = 1;

  const VkImageAspectFlags vkAspect = rvk_image_vkaspect(RvkImageType_Swapchain);
  const VkImageView        vkView   = rvk_vkimageview_create(
      dev, RvkImageType_Swapchain, vkImage, vkFormat, vkAspect, layers, mipLevels);

  return (RvkImage){
      .type        = RvkImageType_Swapchain,
      .phase       = RvkImagePhase_Undefined,
      .caps        = capabilities,
      .vkFormat    = vkFormat,
      .size        = size,
      .layers      = layers,
      .mipLevels   = mipLevels,
      .vkImage     = vkImage,
      .vkImageView = vkView,
  };
}

void rvk_image_destroy(RvkImage* img, RvkDevice* dev) {
  if (img->type != RvkImageType_Swapchain) {
    vkDestroyImage(dev->vkDev, img->vkImage, &dev->vkAlloc);
  }
  vkDestroyImageView(dev->vkDev, img->vkImageView, &dev->vkAlloc);
  if (rvk_mem_valid(img->mem)) {
    rvk_mem_free(img->mem);
  }
}

String rvk_image_type_str(const RvkImageType type) {
  static const String g_names[] = {
      string_static("ColorSource"),
      string_static("ColorSourceCube"),
      string_static("ColorAttachment"),
      string_static("DepthAttachment"),
      string_static("Swapchain"),
  };
  ASSERT(array_elems(g_names) == RvkImageType_Count, "Incorrect number of image-type names");
  return g_names[type];
}

String rvk_image_phase_str(const RvkImagePhase phase) {
  static const String g_names[] = {
      string_static("Undefined"),
      string_static("TransferSource"),
      string_static("TransferDest"),
      string_static("ColorAttachment"),
      string_static("DepthAttachment"),
      string_static("ShaderRead"),
      string_static("Present"),
  };
  ASSERT(array_elems(g_names) == RvkImagePhase_Count, "Incorrect number of image-phase names");
  return g_names[phase];
}

void rvk_image_assert_phase(const RvkImage* img, const RvkImagePhase phase) {
  (void)img;
  (void)phase;
  diag_assert_msg(
      img->phase == phase,
      "Expected image phase '{}'; but found '{}'",
      fmt_text(rvk_image_phase_str(phase)),
      fmt_text(rvk_image_phase_str(img->phase)));
}

void rvk_image_freeze(RvkImage* img) { img->frozen = true; }

void rvk_image_transition(RvkImage* img, const RvkImagePhase phase, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!img->frozen, "Image is frozen");
  diag_assert_msg(
      rvk_image_phase_supported(img->caps, phase),
      "Image does not support the '{}' phase",
      fmt_text(rvk_image_phase_str(phase)));

  const VkImageMemoryBarrier barrier =
      rvk_image_barrier_from_to(img, img->phase, phase, 0, img->mipLevels);

  const VkPipelineStageFlags srcStageFlags = rvk_image_vkpipelinestage(img->phase);
  const VkPipelineStageFlags dstStageFlags = rvk_image_vkpipelinestage(phase);

  img->phase = phase;
  vkCmdPipelineBarrier(vkCmdBuf, srcStageFlags, dstStageFlags, 0, 0, null, 0, null, 1, &barrier);
}

void rvk_image_transition_batch(
    const RvkImageTransition* transitions, const u32 count, VkCommandBuffer vkCmdBuf) {
  VkImageMemoryBarrier barriers[16];
  diag_assert(count <= array_elems(barriers));

  VkPipelineStageFlags srcStageFlags = 0;
  VkPipelineStageFlags dstStageFlags = 0;

  for (u32 i = 0; i != count; ++i) {
    RvkImage*     img   = transitions[i].img;
    RvkImagePhase phase = transitions[i].phase;

    diag_assert_msg(!img->frozen, "Image is frozen");
    diag_assert_msg(
        rvk_image_phase_supported(img->caps, phase),
        "Image does not support the '{}' phase",
        fmt_text(rvk_image_phase_str(phase)));

    srcStageFlags |= rvk_image_vkpipelinestage(img->phase);
    dstStageFlags |= rvk_image_vkpipelinestage(phase);

    barriers[i] = rvk_image_barrier_from_to(img, img->phase, phase, 0, img->mipLevels);
    img->phase  = phase;
  }

  vkCmdPipelineBarrier(
      vkCmdBuf, srcStageFlags, dstStageFlags, 0, 0, null, 0, null, count, barriers);
}

void rvk_image_transition_external(RvkImage* img, const RvkImagePhase phase) {
  diag_assert_msg(!img->frozen, "Image is frozen");
  diag_assert_msg(
      rvk_image_phase_supported(img->caps, phase),
      "Image does not support the '{}' phase",
      fmt_text(rvk_image_phase_str(phase)));

  img->phase = phase;
}

void rvk_image_generate_mipmaps(RvkImage* img, VkCommandBuffer vkCmdBuf) {
  if (img->mipLevels <= 1) {
    return;
  }
  diag_assert_msg(!img->frozen, "Image is frozen");

  MAYBE_UNUSED static const RvkImageCapability g_requiredCaps = RvkImageCapability_TransferSource |
                                                                RvkImageCapability_TransferDest |
                                                                RvkImageCapability_BlitDest;

  diag_assert((g_requiredCaps & img->caps) == g_requiredCaps);
  diag_assert(img->type == RvkImageType_ColorSource || img->type == RvkImageType_ColorSourceCube);

  /**
   * Generate the mipmap levels by copying from the previous level at half the size until all levels
   * have been generated.
   */

  // Transition the first mip to transfer-source.
  // Transition the other mips to transfer-dest.
  {
    VkImageMemoryBarrier barriers[] = {
        rvk_image_barrier_from_to(img, img->phase, RvkImagePhase_TransferSource, 0, 1),
        rvk_image_barrier_from_to(
            img, img->phase, RvkImagePhase_TransferDest, 1, img->mipLevels - 1),
    };
    vkCmdPipelineBarrier(
        vkCmdBuf,
        rvk_image_vkpipelinestage(img->phase),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        null,
        0,
        null,
        array_elems(barriers),
        barriers);
  }

  for (u8 level = 1; level != img->mipLevels; ++level) {
    // Blit from the previous mip-level.
    const VkImageBlit blit = {
        .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .srcSubresource.mipLevel   = level - 1,
        .srcSubresource.layerCount = img->layers,
        .srcOffsets[1].x           = math_max(img->size.width >> (level - 1), 1),
        .srcOffsets[1].y           = math_max(img->size.height >> (level - 1), 1),
        .srcOffsets[1].z           = 1,
        .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .dstSubresource.mipLevel   = level,
        .dstSubresource.layerCount = img->layers,
        .dstOffsets[1].x           = math_max(img->size.width >> level, 1),
        .dstOffsets[1].y           = math_max(img->size.height >> level, 1),
        .dstOffsets[1].z           = 1,
    };
    vkCmdBlitImage(
        vkCmdBuf,
        img->vkImage,
        rvk_image_vklayout(img->type, RvkImagePhase_TransferSource),
        img->vkImage,
        rvk_image_vklayout(img->type, RvkImagePhase_TransferDest),
        1,
        &blit,
        VK_FILTER_LINEAR);

    {
      const VkImageMemoryBarrier barrier = rvk_image_barrier_from_to(
          img, RvkImagePhase_TransferDest, RvkImagePhase_TransferSource, level, 1);
      vkCmdPipelineBarrier(
          vkCmdBuf,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0,
          null,
          0,
          null,
          1,
          &barrier);
    }
  }
  img->phase = RvkImagePhase_TransferSource; // All mips are now at the TransferSource phase.
}

void rvk_image_clear_color(const RvkImage* img, const GeoColor color, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!img->frozen, "Image is frozen");
  rvk_image_assert_phase(img, RvkImagePhase_TransferDest);
  diag_assert(img->type != RvkImageType_DepthAttachment);

  const VkClearColorValue       clearColor = rvk_rend_clear_color(color);
  const VkImageSubresourceRange ranges[]   = {
        {
            .aspectMask     = rvk_image_vkaspect(img->type),
            .baseMipLevel   = 0,
            .levelCount     = img->mipLevels,
            .baseArrayLayer = 0,
            .layerCount     = img->layers,
      },
  };
  vkCmdClearColorImage(
      vkCmdBuf,
      img->vkImage,
      rvk_image_vklayout(img->type, img->phase),
      &clearColor,
      array_elems(ranges),
      ranges);
}

void rvk_image_clear_depth(const RvkImage* img, const f32 depth, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!img->frozen, "Image is frozen");
  rvk_image_assert_phase(img, RvkImagePhase_TransferDest);
  diag_assert(img->type == RvkImageType_DepthAttachment);

  const VkClearDepthStencilValue clearValue = {.depth = depth};
  const VkImageSubresourceRange  ranges[]   = {
         {
             .aspectMask     = rvk_image_vkaspect(img->type),
             .baseMipLevel   = 0,
             .levelCount     = img->mipLevels,
             .baseArrayLayer = 0,
             .layerCount     = img->layers,
      },
  };
  vkCmdClearDepthStencilImage(
      vkCmdBuf,
      img->vkImage,
      rvk_image_vklayout(img->type, img->phase),
      &clearValue,
      array_elems(ranges),
      ranges);
}

void rvk_image_copy(const RvkImage* src, RvkImage* dest, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!dest->frozen, "Destination image is frozen");
  rvk_image_assert_phase(src, RvkImagePhase_TransferSource);
  rvk_image_assert_phase(dest, RvkImagePhase_TransferDest);
  diag_assert_msg(rvk_size_equal(src->size, dest->size), "Image copy requires matching sizes");
  diag_assert_msg(src->layers == dest->layers, "Image copy requires matching layer counts");
  diag_assert_msg(src->vkFormat == dest->vkFormat, "Image copy requires matching formats");

  const VkImageCopy regions[] = {
      {
          .srcSubresource.aspectMask = rvk_image_vkaspect(src->type),
          .srcSubresource.mipLevel   = 0,
          .srcSubresource.layerCount = src->layers,
          .dstSubresource.aspectMask = rvk_image_vkaspect(dest->type),
          .dstSubresource.mipLevel   = 0,
          .dstSubresource.layerCount = dest->layers,
          .extent.width              = src->size.width,
          .extent.height             = src->size.height,
          .extent.depth              = 1,
      },
  };
  vkCmdCopyImage(
      vkCmdBuf,
      src->vkImage,
      rvk_image_vklayout(src->type, src->phase),
      dest->vkImage,
      rvk_image_vklayout(dest->type, dest->phase),
      array_elems(regions),
      regions);
}

void rvk_image_blit(const RvkImage* src, RvkImage* dest, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!dest->frozen, "Destination image is frozen");
  rvk_image_assert_phase(src, RvkImagePhase_TransferSource);
  rvk_image_assert_phase(dest, RvkImagePhase_TransferDest);
  diag_assert_msg(src->layers == dest->layers, "Image blit requires matching layer counts");
  diag_assert_msg(dest->caps & RvkImageCapability_BlitDest, "Dest image does not support blitting");

  const VkImageBlit regions[] = {
      {
          .srcSubresource.aspectMask = rvk_image_vkaspect(src->type),
          .srcSubresource.mipLevel   = 0,
          .srcSubresource.layerCount = src->layers,
          .srcOffsets[1].x           = src->size.width,
          .srcOffsets[1].y           = src->size.height,
          .srcOffsets[1].z           = 1,
          .dstSubresource.aspectMask = rvk_image_vkaspect(dest->type),
          .dstSubresource.mipLevel   = 0,
          .dstSubresource.layerCount = dest->layers,
          .dstOffsets[1].x           = dest->size.width,
          .dstOffsets[1].y           = dest->size.height,
          .dstOffsets[1].z           = 1,
      },
  };

  const bool srcIsDepth = src->type == RvkImageType_DepthAttachment;
  vkCmdBlitImage(
      vkCmdBuf,
      src->vkImage,
      rvk_image_vklayout(src->type, src->phase),
      dest->vkImage,
      rvk_image_vklayout(dest->type, dest->phase),
      array_elems(regions),
      regions,
      srcIsDepth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
}

void rvk_image_transfer_ownership(
    const RvkImage* img,
    VkCommandBuffer srcCmdBuf,
    VkCommandBuffer dstCmdBuf,
    const u32       srcQueueFamIdx,
    const u32       dstQueueFamIdx) {
  if (srcQueueFamIdx == dstQueueFamIdx) {
    return;
  }
  diag_assert_msg(!img->frozen, "Image is frozen");
  const VkPipelineStageFlags stageFlags = rvk_image_vkpipelinestage(img->phase);

  // Release the image on the source queue.
  const VkImageMemoryBarrier releaseBarrier = rvk_image_barrier(
      img,
      srcQueueFamIdx,
      dstQueueFamIdx,
      rvk_image_vklayout(img->type, img->phase),
      rvk_image_vklayout(img->type, img->phase),
      rvk_image_vkaccess_write(img->phase),
      0,
      0,
      img->mipLevels);
  vkCmdPipelineBarrier(srcCmdBuf, stageFlags, stageFlags, 0, 0, null, 0, null, 1, &releaseBarrier);

  // Acquire the image on the destination queue.
  const VkImageMemoryBarrier acquireBarrier = rvk_image_barrier(
      img,
      srcQueueFamIdx,
      dstQueueFamIdx,
      rvk_image_vklayout(img->type, img->phase),
      rvk_image_vklayout(img->type, img->phase),
      0,
      rvk_image_vkaccess_read(img->phase) | rvk_image_vkaccess_write(img->phase),
      0,
      img->mipLevels);
  vkCmdPipelineBarrier(dstCmdBuf, stageFlags, stageFlags, 0, 0, null, 0, null, 1, &acquireBarrier);
}
