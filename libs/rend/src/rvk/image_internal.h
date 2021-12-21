#pragma once
#include "rend_size.h"

#include "mem_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkImageFlags_None            = 0,
  RvkImageFlags_GenerateMipMaps = 1 << 0,
} RvkImageFlags;

typedef enum {
  RvkImageType_ColorSource,
  RvkImageType_ColorAttachment,
  RvkImageType_DepthAttachment,
  RvkImageType_Swapchain,

  RvkImageType_Count,
} RvkImageType;

typedef struct sRvkImage {
  RvkImageType  type : 8;
  RvkImageFlags flags : 8;
  u8            mipLevels;
  VkFormat      vkFormat;
  RendSize      size;
  VkImage       vkImage;
  VkImageView   vkImageView;
  VkImageLayout vkImageLayout;
  RvkMem        mem;
} RvkImage;

RvkImage rvk_image_create_source_color(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_attach_color(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_attach_depth(RvkDevice*, VkFormat, RendSize size, RvkImageFlags);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RendSize size, RvkImageFlags);
void     rvk_image_destroy(RvkImage*, RvkDevice*);

String rvk_image_type_str(RvkImageType);
void   rvk_image_transition(RvkImage*, VkCommandBuffer, VkImageLayout);
void   rvk_image_transition_external(RvkImage*, VkImageLayout);
