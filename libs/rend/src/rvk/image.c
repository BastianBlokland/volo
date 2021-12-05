#include "core_annotation.h"
#include "core_array.h"

#include "image_internal.h"

static VkImageAspectFlags rvk_image_aspect(const RvkImageType type) {
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

static VkImageView rvk_imageview_create(
    RvkDevice*         dev,
    VkImage            vkImage,
    VkFormat           vkFormat,
    VkImageAspectFlags vkAspect,
    u32                mipLevels) {

  VkImageViewCreateInfo createInfo = {
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

RvkImage rvk_image_create_swapchain(
    RvkDevice* dev, VkImage vkImage, VkFormat vkFormat, const RendSize size) {

  const RvkImageType       type      = RvkImageType_Swapchain;
  const VkImageAspectFlags vkAspect  = rvk_image_aspect(type);
  const u32                mipLevels = 1;
  const VkImageView vkView = rvk_imageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  return (RvkImage){
      .dev         = dev,
      .type        = type,
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
