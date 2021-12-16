#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"

#include "device_internal.h"
#include "image_internal.h"

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

static VkImageUsageFlags rvk_image_vkusage(const RvkImageType type) {
  switch (type) {
  case RvkImageType_ColorSource:
    return VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    const u32               mipLevels) {

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
    const u32                mipLevels) {

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
    RvkDevice* dev, const RvkImageType type, const VkFormat vkFormat, const RendSize size) {

  const VkImageAspectFlags vkAspect  = rvk_image_vkaspect(type);
  const VkImageAspectFlags vkUsage   = rvk_image_vkusage(type);
  const u32                mipLevels = 1;

  const VkImage     vkImage = rvk_vkimage_create(dev, size, vkFormat, vkUsage, mipLevels);
  const VkImageView vkView  = rvk_vkimageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(dev->vkDev, vkImage, &memReqs);

  const RvkMemLoc memLoc = RvkMemLoc_Dev;
  const RvkMem    mem    = rvk_mem_alloc_req(dev->memPool, memLoc, RvkMemAccess_NonLinear, memReqs);

  rvk_mem_bind_image(mem, vkImage);

  return (RvkImage){
      .dev         = dev,
      .type        = type,
      .size        = size,
      .mipLevels   = mipLevels,
      .vkFormat    = vkFormat,
      .vkImage     = vkImage,
      .vkImageView = vkView,
      .mem         = mem,
  };
}

RvkImage
rvk_image_create_source_color(RvkDevice* dev, const VkFormat vkFormat, const RendSize size) {
  diag_assert(rvk_format_info(vkFormat).channels == 4);
  return rvk_image_create_backed(dev, RvkImageType_ColorSource, vkFormat, size);
}

RvkImage
rvk_image_create_attach_color(RvkDevice* dev, const VkFormat vkFormat, const RendSize size) {
  diag_assert(rvk_format_info(vkFormat).channels == 4);
  return rvk_image_create_backed(dev, RvkImageType_ColorAttachment, vkFormat, size);
}

RvkImage
rvk_image_create_attach_depth(RvkDevice* dev, const VkFormat vkFormat, const RendSize size) {
  diag_assert(rvk_format_info(vkFormat).channels == 1);
  return rvk_image_create_backed(dev, RvkImageType_DepthAttachment, vkFormat, size);
}

RvkImage rvk_image_create_swapchain(
    RvkDevice* dev, VkImage vkImage, VkFormat vkFormat, const RendSize size) {

  const VkImageAspectFlags vkAspect  = VK_IMAGE_ASPECT_COLOR_BIT;
  const u32                mipLevels = 1;
  const VkImageView vkView = rvk_vkimageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  return (RvkImage){
      .dev         = dev,
      .type        = RvkImageType_Swapchain,
      .size        = size,
      .mipLevels   = mipLevels,
      .vkFormat    = vkFormat,
      .vkImage     = vkImage,
      .vkImageView = vkView,
  };
}

void rvk_image_destroy(RvkImage* img) {
  if (img->type != RvkImageType_Swapchain) {
    vkDestroyImage(img->dev->vkDev, img->vkImage, &img->dev->vkAlloc);
  }
  vkDestroyImageView(img->dev->vkDev, img->vkImageView, &img->dev->vkAlloc);
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
