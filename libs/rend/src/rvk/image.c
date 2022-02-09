#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"

#include "device_internal.h"
#include "image_internal.h"

static VkAccessFlags rvk_image_vkaccess_read(const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    return 0;
  case RvkImagePhase_TransferSource:
    return VK_ACCESS_TRANSFER_READ_BIT;
  case RvkImagePhase_TransferDest:
    return 0;
  case RvkImagePhase_ColorAttachment:
    return 0;
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
    return 0;
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
    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  case RvkImagePhase_TransferSource:
  case RvkImagePhase_TransferDest:
    return VK_PIPELINE_STAGE_TRANSFER_BIT;
  case RvkImagePhase_ColorAttachment:
    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  case RvkImagePhase_DepthAttachment:
    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  case RvkImagePhase_ShaderRead:
    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  case RvkImagePhase_Present:
    return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static VkImageLayout rvk_image_vklayout(const RvkImagePhase phase) {
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
    return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  case RvkImagePhase_ShaderRead:
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
  case RvkImageType_ColorAttachment:
  case RvkImageType_Swapchain:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  case RvkImageType_DepthAttachment:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  default:
    return 0;
  }
}

static VkImageUsageFlags rvk_image_vkusage(const RvkImageType type, const u8 mipLevels) {
  switch (type) {
  case RvkImageType_ColorSource: {
    VkImageUsageFlags flags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mipLevels > 1) {
      flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    return flags;
  }
  case RvkImageType_ColorAttachment:
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  case RvkImageType_DepthAttachment:
    return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  case RvkImageType_Swapchain:
    // Swapchain images cannot be created manually.
    break;
  case RvkImageType_Count:
    break;
  }
  diag_crash();
}

static VkFormatFeatureFlags rvk_image_format_features(const RvkImageType type, const u8 mipLevels) {
  switch (type) {
  case RvkImageType_ColorSource: {
    VkFormatFeatureFlags flags =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (mipLevels > 1) {
      flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    return flags;
  }
  case RvkImageType_ColorAttachment:
    return VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  case RvkImageType_DepthAttachment:
    return VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  case RvkImageType_Swapchain:
    // Swapchain images cannot be created manually.
    break;
  case RvkImageType_Count:
    break;
  }
  diag_crash();
}

static void rvk_image_barrier(
    VkCommandBuffer            vkCmdBuf,
    const RvkImage*            image,
    const VkImageLayout        oldLayout,
    const VkImageLayout        newLayout,
    const VkAccessFlags        srcAccess,
    const VkAccessFlags        dstAccess,
    const VkPipelineStageFlags srcStageFlags,
    const VkPipelineStageFlags dstStageFlags,
    const u8                   baseMip,
    const u8                   mipLevels) {

  const VkImageMemoryBarrier barrier = {
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout                       = oldLayout,
      .newLayout                       = newLayout,
      .srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .image                           = image->vkImage,
      .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .subresourceRange.baseMipLevel   = baseMip,
      .subresourceRange.levelCount     = mipLevels,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount     = image->layers,
      .srcAccessMask                   = srcAccess,
      .dstAccessMask                   = dstAccess,
  };
  vkCmdPipelineBarrier(vkCmdBuf, srcStageFlags, dstStageFlags, 0, 0, null, 0, null, 1, &barrier);
}

static void rvk_image_barrier_from_to(
    VkCommandBuffer     vkCmdBuf,
    const RvkImage*     image,
    const RvkImagePhase from,
    const RvkImagePhase to,
    const u8            baseMip,
    const u8            mipLevels) {
  rvk_image_barrier(
      vkCmdBuf,
      image,
      rvk_image_vklayout(from),
      rvk_image_vklayout(to),
      rvk_image_vkaccess_write(from),
      rvk_image_vkaccess_read(to) | rvk_image_vkaccess_write(to),
      rvk_image_vkpipelinestage(from),
      rvk_image_vkpipelinestage(to),
      baseMip,
      mipLevels);
}

static VkImage rvk_vkimage_create(
    RvkDevice*              dev,
    const RvkSize           size,
    const VkFormat          vkFormat,
    const VkImageUsageFlags vkImgUsages,
    const u32               layers,
    const u8                mipLevels) {

  const VkImageCreateInfo imageInfo = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
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
    const VkImage            vkImage,
    const VkFormat           vkFormat,
    const VkImageAspectFlags vkAspect,
    const u32                layers,
    const u8                 mipLevels) {

  const VkImageViewCreateInfo createInfo = {
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image                           = vkImage,
      .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
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
    RvkDevice*         dev,
    const RvkImageType type,
    const VkFormat     vkFormat,
    const RvkSize      size,
    const u32          layers,
    const u8           mipLevels) {

  const VkFormatFeatureFlags vkFormatFeatures = rvk_image_format_features(type, mipLevels);
  if (UNLIKELY(!rvk_device_format_supported(dev, vkFormat, vkFormatFeatures))) {
    diag_crash_msg("Image format {} unsupported", fmt_text(rvk_format_info(vkFormat).name));
  }

  const VkImageAspectFlags vkAspect = rvk_image_vkaspect(type);
  const VkImageUsageFlags  vkUsage  = rvk_image_vkusage(type, mipLevels);
  const VkImage vkImage = rvk_vkimage_create(dev, size, vkFormat, vkUsage, layers, mipLevels);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(dev->vkDev, vkImage, &memReqs);

  const RvkMemLoc memLoc = RvkMemLoc_Dev;
  const RvkMem    mem    = rvk_mem_alloc_req(dev->memPool, memLoc, RvkMemAccess_NonLinear, memReqs);
  rvk_mem_bind_image(mem, vkImage);

  const VkImageView vkView =
      rvk_vkimageview_create(dev, vkImage, vkFormat, vkAspect, layers, mipLevels);

  return (RvkImage){
      .type        = type,
      .mipLevels   = mipLevels,
      .layers      = layers,
      .vkFormat    = vkFormat,
      .size        = size,
      .vkImage     = vkImage,
      .vkImageView = vkView,
      .mem         = mem,
  };
}

RvkImage rvk_image_create_source_color(
    RvkDevice*     dev,
    const VkFormat vkFormat,
    const RvkSize  size,
    const u32      layers,
    const u8       mipLevels) {
  return rvk_image_create_backed(dev, RvkImageType_ColorSource, vkFormat, size, layers, mipLevels);
}

RvkImage
rvk_image_create_attach_color(RvkDevice* dev, const VkFormat vkFormat, const RvkSize size) {
  diag_assert(rvk_format_info(vkFormat).channels == 4);
  const u32 layers    = 1;
  const u8  mipLevels = 1;
  return rvk_image_create_backed(
      dev, RvkImageType_ColorAttachment, vkFormat, size, layers, mipLevels);
}

RvkImage
rvk_image_create_attach_depth(RvkDevice* dev, const VkFormat vkFormat, const RvkSize size) {
  diag_assert(rvk_format_info(vkFormat).channels == 1 || rvk_format_info(vkFormat).channels == 2);
  const u32 layers    = 1;
  const u8  mipLevels = 1;
  return rvk_image_create_backed(
      dev, RvkImageType_DepthAttachment, vkFormat, size, layers, mipLevels);
}

RvkImage
rvk_image_create_swapchain(RvkDevice* dev, VkImage vkImage, VkFormat vkFormat, const RvkSize size) {
  (void)dev;
  return (RvkImage){
      .type      = RvkImageType_Swapchain,
      .layers    = 1,
      .mipLevels = 1,
      .vkFormat  = vkFormat,
      .size      = size,
      .vkImage   = vkImage,
  };
}

void rvk_image_destroy(RvkImage* img, RvkDevice* dev) {
  if (img->type != RvkImageType_Swapchain) {
    vkDestroyImage(dev->vkDev, img->vkImage, &dev->vkAlloc);
    vkDestroyImageView(dev->vkDev, img->vkImageView, &dev->vkAlloc);
  }
  if (rvk_mem_valid(img->mem)) {
    rvk_mem_free(img->mem);
  }
}

String rvk_image_type_str(const RvkImageType type) {
  static const String g_names[] = {
      string_static("ColorSource"),
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

void rvk_image_assert_phase(const RvkImage* image, const RvkImagePhase phase) {
  (void)image;
  (void)phase;
  diag_assert_msg(
      image->phase == phase,
      "Expected image phase '{}'; but found '{}'",
      fmt_text(rvk_image_phase_str(phase)),
      fmt_text(rvk_image_phase_str(image->phase)));
}

void rvk_image_transition(RvkImage* image, VkCommandBuffer vkCmdBuf, const RvkImagePhase phase) {
  rvk_image_barrier_from_to(vkCmdBuf, image, image->phase, phase, 0, image->mipLevels);
  image->phase = phase;
}

void rvk_image_transition_external(RvkImage* image, const RvkImagePhase phase) {
  image->phase = phase;
}

void rvk_image_generate_mipmaps(RvkImage* image, VkCommandBuffer vkCmdBuf) {
  if (image->mipLevels <= 1) {
    return;
  }

  /**
   * Generate the mipmap levels by copying from the previous level at half the size until all levels
   * have been generated.
   */

  // Transition the first mip to transfer-source.
  rvk_image_barrier_from_to(vkCmdBuf, image, image->phase, RvkImagePhase_TransferSource, 0, 1);
  // Transition the other mips to transfer-dest.
  rvk_image_barrier_from_to(
      vkCmdBuf, image, image->phase, RvkImagePhase_TransferDest, 1, image->mipLevels - 1);

  for (u8 level = 1; level != image->mipLevels; ++level) {
    // Blit from the previous mip-level.
    const VkImageBlit blit = {
        .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .srcSubresource.mipLevel   = level - 1,
        .srcSubresource.layerCount = image->layers,
        .srcOffsets[1].x           = math_max(image->size.width >> (level - 1), 1),
        .srcOffsets[1].y           = math_max(image->size.height >> (level - 1), 1),
        .srcOffsets[1].z           = 1,
        .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .dstSubresource.mipLevel   = level,
        .dstSubresource.layerCount = image->layers,
        .dstOffsets[1].x           = math_max(image->size.width >> level, 1),
        .dstOffsets[1].y           = math_max(image->size.height >> level, 1),
        .dstOffsets[1].z           = 1,
    };
    vkCmdBlitImage(
        vkCmdBuf,
        image->vkImage,
        rvk_image_vklayout(RvkImagePhase_TransferSource),
        image->vkImage,
        rvk_image_vklayout(RvkImagePhase_TransferDest),
        1,
        &blit,
        VK_FILTER_LINEAR);

    rvk_image_barrier_from_to(
        vkCmdBuf, image, RvkImagePhase_TransferDest, RvkImagePhase_TransferSource, level, 1);
  }
  image->phase = RvkImagePhase_TransferSource; // All mips are now at the TransferSource phase.
}

void rvk_image_copy(const RvkImage* src, RvkImage* dest, VkCommandBuffer vkCmdBuf) {
  rvk_image_assert_phase(src, RvkImagePhase_TransferSource);
  rvk_image_assert_phase(dest, RvkImagePhase_TransferDest);
  diag_assert_msg(rvk_size_equal(src->size, dest->size), "Image copy requires matching sizes");

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
      rvk_image_vklayout(src->phase),
      dest->vkImage,
      rvk_image_vklayout(dest->phase),
      array_elems(regions),
      regions);
}

void rvk_image_blit(const RvkImage* src, RvkImage* dest, VkCommandBuffer vkCmdBuf) {
  rvk_image_assert_phase(src, RvkImagePhase_TransferSource);
  rvk_image_assert_phase(dest, RvkImagePhase_TransferDest);

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
  vkCmdBlitImage(
      vkCmdBuf,
      src->vkImage,
      rvk_image_vklayout(src->phase),
      dest->vkImage,
      rvk_image_vklayout(dest->phase),
      array_elems(regions),
      regions,
      VK_FILTER_LINEAR);
}

void rvk_image_clear(const RvkImage* img, const GeoColor color, VkCommandBuffer vkCmdBuf) {
  rvk_image_assert_phase(img, RvkImagePhase_TransferDest);

  const VkClearColorValue       clearColor = *(VkClearColorValue*)&color;
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
      rvk_image_vklayout(img->phase),
      &clearColor,
      array_elems(ranges),
      ranges);
}
