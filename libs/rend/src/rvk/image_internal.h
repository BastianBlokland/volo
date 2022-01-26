#pragma once
#include "geo_color.h"

#include "mem_internal.h"
#include "types_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkImagePhase_Undefined,
  RvkImagePhase_TransferSource,
  RvkImagePhase_TransferDest,
  RvkImagePhase_ColorAttachment,
  RvkImagePhase_DepthAttachment,
  RvkImagePhase_ShaderRead,
  RvkImagePhase_Present,

  RvkImagePhase_Count,
} RvkImagePhase;

typedef enum {
  RvkImageType_ColorSource,
  RvkImageType_ColorAttachment,
  RvkImageType_DepthAttachment,
  RvkImageType_Swapchain,

  RvkImageType_Count,
} RvkImageType;

typedef struct sRvkImage {
  RvkImageType  type : 8;
  RvkImagePhase phase : 8;
  u8            mipLevels;
  VkFormat      vkFormat;
  RvkSize       size;
  VkImage       vkImage;
  VkImageView   vkImageView;
  RvkMem        mem;
} RvkImage;

RvkImage rvk_image_create_source_color(RvkDevice*, VkFormat, RvkSize size, u8 mipLevels);
RvkImage rvk_image_create_attach_color(RvkDevice*, VkFormat, RvkSize size);
RvkImage rvk_image_create_attach_depth(RvkDevice*, VkFormat, RvkSize size);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RvkSize size);
void     rvk_image_destroy(RvkImage*, RvkDevice*);

String rvk_image_type_str(RvkImageType);
String rvk_image_phase_str(RvkImagePhase);

void rvk_image_assert_phase(const RvkImage*, RvkImagePhase);
void rvk_image_transition(RvkImage*, VkCommandBuffer, RvkImagePhase);
void rvk_image_transition_external(RvkImage*, RvkImagePhase);

void rvk_image_generate_mipmaps(RvkImage*, VkCommandBuffer);
void rvk_image_copy(const RvkImage* src, RvkImage* dest, VkCommandBuffer);
void rvk_image_blit(const RvkImage* src, RvkImage* dest, VkCommandBuffer);
void rvk_image_clear(const RvkImage*, GeoColor, VkCommandBuffer);
