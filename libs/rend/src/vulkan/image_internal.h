#pragma once
#include "gap_vector.h"

#include "device_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RendVkImageType_ColorSource,
  RendVkImageType_ColorAttachment,
  RendVkImageType_DepthAttachment,
  RendVkImageType_Swapchain,

  RendVkImageType_Count,
} RendVkImageType;

typedef struct {
  RendVkDevice*   device;
  RendVkImageType type;
  GapVector       size;
  u32             mipLevels;
  VkFormat        vkFormat;
  VkImage         vkImage;
  VkImageView     vkImageView;
} RendVkImage;

RendVkImage rend_vk_image_create_swapchain(RendVkDevice*, VkImage, VkFormat, GapVector size);
void        rend_vk_image_destroy(RendVkImage*);
String      rend_vk_image_type_str(RendVkImageType);
