#pragma once
#include "rend_size.h"

#include "device_internal.h"
#include "mem_internal.h"

typedef enum {
  RvkImageType_ColorSource,
  RvkImageType_ColorAttachment,
  RvkImageType_DepthAttachment,
  RvkImageType_Swapchain,

  RvkImageType_Count,
} RvkImageType;

typedef struct {
  RvkDevice*   dev;
  RvkImageType type;
  RendSize     size;
  u32          mipLevels;
  VkFormat     vkFormat;
  VkImage      vkImage;
  VkImageView  vkImageView;
  RvkMem       mem;
} RvkImage;

RvkImage rvk_image_create_colorsource(RvkDevice*, VkFormat, RendSize size);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RendSize size);
void     rvk_image_destroy(RvkImage*);
String   rvk_image_type_str(RvkImageType);
