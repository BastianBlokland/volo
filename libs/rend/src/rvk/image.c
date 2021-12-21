#include "core_annotation.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"

#include "device_internal.h"
#include "image_internal.h"

static void rvk_image_barrier(
    VkCommandBuffer            buffer,
    const RvkImage*            image,
    const VkImageLayout        oldLayout,
    const VkImageLayout        newLayout,
    const VkAccessFlags        srcAccess,
    const VkAccessFlags        dstAccess,
    const VkPipelineStageFlags srcStageFlags,
    const VkPipelineStageFlags dstStageFlags) {

  const VkImageMemoryBarrier barrier = {
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout                       = oldLayout,
      .newLayout                       = newLayout,
      .srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .image                           = image->vkImage,
      .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .subresourceRange.baseMipLevel   = 0,
      .subresourceRange.levelCount     = image->mipLevels,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount     = 1,
      .srcAccessMask                   = srcAccess,
      .dstAccessMask                   = dstAccess,
  };
  vkCmdPipelineBarrier(buffer, srcStageFlags, dstStageFlags, 0, 0, null, 0, null, 1, &barrier);
}

static VkAccessFlags rvk_image_vkaccess(const RvkImagePhase phase) {
  switch (phase) {
  case RvkImagePhase_Undefined:
    return VK_ACCESS_NONE_KHR;
  case RvkImagePhase_TransferSource:
    return VK_ACCESS_TRANSFER_READ_BIT;
  case RvkImagePhase_TransferDest:
    return VK_ACCESS_TRANSFER_WRITE_BIT;
  case RvkImagePhase_ColorAttachment:
    return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  case RvkImagePhase_ShaderRead:
    // TODO: Update to 'VK_ACCESS_SHADER_READ_BIT'.
    return VK_ACCESS_NONE_KHR;
  case RvkImagePhase_Present:
    return VK_ACCESS_NONE_KHR;
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
  case RvkImagePhase_ShaderRead:
    // TODO: Update to:
    // 'VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT'.
    return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
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
  case RvkImagePhase_ShaderRead:
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  case RvkImagePhase_Present:
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  case RvkImagePhase_Count:
    break;
  }
  diag_crash_msg("Unsupported image phase");
}

static u32 rvk_compute_miplevels(const RendSize size) {
  /**
   * Check how many times we can cut the image in half before both sides hit 1 pixel.
   */
  const u32 biggestSide = math_max(size.width, size.height);
  return 32 - bits_clz_32(biggestSide);
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

static VkImageUsageFlags rvk_image_vkusage(const RvkImageType type, const RvkImageFlags flags) {
  switch (type) {
  case RvkImageType_ColorSource:
    return VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
           (flags & RvkImageFlags_GenerateMipMaps ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  case RvkImageType_ColorAttachment:
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

static VkImage rvk_vkimage_create(
    RvkDevice*              dev,
    const RendSize          size,
    const VkFormat          vkFormat,
    const VkImageUsageFlags vkImgUsages,
    const u8                mipLevels) {

  const VkImageCreateInfo imageInfo = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType     = VK_IMAGE_TYPE_2D,
      .extent.width  = size.width,
      .extent.height = size.height,
      .extent.depth  = 1,
      .mipLevels     = mipLevels,
      .arrayLayers   = 1,
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
      .subresourceRange.layerCount     = 1,
  };
  VkImageView result;
  rvk_call(vkCreateImageView, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static RvkImage rvk_image_create_backed(
    RvkDevice*          dev,
    const RvkImageType  type,
    const VkFormat      vkFormat,
    const RendSize      size,
    const RvkImageFlags flags) {

  const VkImageAspectFlags vkAspect = rvk_image_vkaspect(type);
  const VkImageAspectFlags vkUsage  = rvk_image_vkusage(type, flags);
  const u8 mipLevels = flags & RvkImageFlags_GenerateMipMaps ? rvk_compute_miplevels(size) : 1;

  const VkImage vkImage = rvk_vkimage_create(dev, size, vkFormat, vkUsage, mipLevels);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(dev->vkDev, vkImage, &memReqs);

  const RvkMemLoc memLoc = RvkMemLoc_Dev;
  const RvkMem    mem    = rvk_mem_alloc_req(dev->memPool, memLoc, RvkMemAccess_NonLinear, memReqs);
  rvk_mem_bind_image(mem, vkImage);

  const VkImageView vkView = rvk_vkimageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  return (RvkImage){
      .type        = type,
      .flags       = flags,
      .mipLevels   = mipLevels,
      .vkFormat    = vkFormat,
      .size        = size,
      .vkImage     = vkImage,
      .vkImageView = vkView,
      .mem         = mem,
  };
}

RvkImage rvk_image_create_source_color(
    RvkDevice* dev, const VkFormat vkFormat, const RendSize size, const RvkImageFlags flags) {
  diag_assert(rvk_format_info(vkFormat).channels == 4);
  return rvk_image_create_backed(dev, RvkImageType_ColorSource, vkFormat, size, flags);
}

RvkImage rvk_image_create_attach_color(
    RvkDevice* dev, const VkFormat vkFormat, const RendSize size, const RvkImageFlags flags) {
  diag_assert(!(flags & RvkImageFlags_GenerateMipMaps));
  diag_assert(rvk_format_info(vkFormat).channels == 4);
  return rvk_image_create_backed(dev, RvkImageType_ColorAttachment, vkFormat, size, flags);
}

RvkImage rvk_image_create_attach_depth(
    RvkDevice* dev, const VkFormat vkFormat, const RendSize size, const RvkImageFlags flags) {
  diag_assert(!(flags & RvkImageFlags_GenerateMipMaps));
  diag_assert(rvk_format_info(vkFormat).channels == 1);
  return rvk_image_create_backed(dev, RvkImageType_DepthAttachment, vkFormat, size, flags);
}

RvkImage rvk_image_create_swapchain(
    RvkDevice*          dev,
    VkImage             vkImage,
    VkFormat            vkFormat,
    const RendSize      size,
    const RvkImageFlags flags) {
  diag_assert(!(flags & RvkImageFlags_GenerateMipMaps));

  const VkImageAspectFlags vkAspect  = VK_IMAGE_ASPECT_COLOR_BIT;
  const u8                 mipLevels = 1;
  const VkImageView vkView = rvk_vkimageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  return (RvkImage){
      .type        = RvkImageType_Swapchain,
      .flags       = flags,
      .mipLevels   = mipLevels,
      .vkFormat    = vkFormat,
      .size        = size,
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
  static const String names[] = {
      string_static("ColorSource"),
      string_static("ColorAttachment"),
      string_static("DepthAttachment"),
      string_static("Swapchain"),
  };
  ASSERT(array_elems(names) == RvkImageType_Count, "Incorrect number of image-type names");
  return names[type];
}

String rvk_image_phase_str(const RvkImagePhase phase) {
  static const String names[] = {
      string_static("Undefined"),
      string_static("TransferSource"),
      string_static("TransferDest"),
      string_static("ColorAttachment"),
      string_static("ShaderRead"),
      string_static("Present"),
  };
  ASSERT(array_elems(names) == RvkImagePhase_Count, "Incorrect number of image-phase names");
  return names[phase];
}

void rvk_image_assert_phase(const RvkImage* image, const RvkImagePhase phase) {
  (void)image;
  (void)phase;
  diag_assert_msg(
      image->phase == phase,
      "Expected image phase {} but found {}",
      fmt_text(rvk_image_phase_str(phase)),
      fmt_text(rvk_image_phase_str(image->phase)));
}

void rvk_image_transition(RvkImage* image, VkCommandBuffer vkCmdBuf, const RvkImagePhase phase) {
  rvk_image_barrier(
      vkCmdBuf,
      image,
      rvk_image_vklayout(image->phase),
      rvk_image_vklayout(phase),
      rvk_image_vkaccess(image->phase),
      rvk_image_vkaccess(phase),
      rvk_image_vkpipelinestage(image->phase),
      rvk_image_vkpipelinestage(phase));
  image->phase = phase;
}

void rvk_image_transition_external(RvkImage* image, const RvkImagePhase phase) {
  image->phase = phase;
}
