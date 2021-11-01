#include "core_annotation.h"
#include "core_array.h"

#include "image_internal.h"

static VkImageAspectFlags rend_vk_image_aspect(const RendVkImageType type) {
  switch (type) {
  case RendVkImageType_ColorSource:
  case RendVkImageType_ColorAttachment:
  case RendVkImageType_Swapchain:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  case RendVkImageType_DepthAttachment:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  default:
    return 0;
  }
}

static VkImageView rend_vk_imageview_create(
    RendVkDevice*      dev,
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
  rend_vk_call(vkCreateImageView, dev->vkDevice, &createInfo, &dev->vkAllocHost, &result);
  return result;
}

RendVkImage rend_vk_image_create_swapchain(
    RendVkDevice* dev, VkImage vkImage, VkFormat vkFormat, const RendSize size) {

  const RendVkImageType    type      = RendVkImageType_Swapchain;
  const VkImageAspectFlags vkAspect  = rend_vk_image_aspect(type);
  const u32                mipLevels = 1;
  const VkImageView vkView = rend_vk_imageview_create(dev, vkImage, vkFormat, vkAspect, mipLevels);

  return (RendVkImage){
      .device      = dev,
      .type        = type,
      .size        = size,
      .mipLevels   = mipLevels,
      .vkFormat    = vkFormat,
      .vkImage     = vkImage,
      .vkImageView = vkView,
  };
}

void rend_vk_image_destroy(RendVkImage* img) {
  if (img->type != RendVkImageType_Swapchain) {
    vkDestroyImage(img->device->vkDevice, img->vkImage, &img->device->vkAllocHost);
  }
  vkDestroyImageView(img->device->vkDevice, img->vkImageView, &img->device->vkAllocHost);
}

String rend_vk_image_type_str(const RendVkImageType type) {
  static const String names[] = {
      string_static("ColorSource"),
      string_static("ColorAttachment"),
      string_static("DepthAttachment"),
      string_static("Swapchain"),
  };
  ASSERT(array_elems(names) == RendVkImageType_Count, "Incorrect number of image-type names");
  return names[type];
}
