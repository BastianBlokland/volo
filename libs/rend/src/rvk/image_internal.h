#pragma once
#include "rend_size.h"

#include "mem_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkImageFlags_None = 0,
} RvkImageFlags;

typedef enum {
  RvkImageType_ColorSource,
  RvkImageType_ColorAttachment,
  RvkImageType_DepthAttachment,
  RvkImageType_Swapchain,

  RvkImageType_Count,
} RvkImageType;

typedef struct sRvkImage {
  RvkDevice*    dev;
  RvkImageType  type;
  RvkImageFlags flags;
  RendSize      size;
  u32           mipLevels;
  VkFormat      vkFormat;
  VkImage       vkImage;
  VkImageView   vkImageView;
  RvkMem        mem;
} RvkImage;

RvkImage rvk_image_create_source_color(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_attach_color(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_attach_depth(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RendSize size, RvkImageFlags);
void     rvk_image_destroy(RvkImage*);
String   rvk_image_type_str(RvkImageType);
