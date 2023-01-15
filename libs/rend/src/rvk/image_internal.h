#pragma once

#include "mem_internal.h"
#include "types_internal.h"

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum eRvkImagePhase {
  RvkImagePhase_Undefined,
  RvkImagePhase_TransferSource,
  RvkImagePhase_TransferDest,
  RvkImagePhase_ColorAttachment,
  RvkImagePhase_DepthAttachment,
  RvkImagePhase_ShaderRead,
  RvkImagePhase_Present,

  RvkImagePhase_Count,
} RvkImagePhase;

typedef enum eRvkImageType {
  RvkImageType_ColorSource,
  RvkImageType_ColorSourceCube,
  RvkImageType_ColorAttachment,
  RvkImageType_DepthAttachment,
  RvkImageType_Swapchain,

  RvkImageType_Count,
} RvkImageType;

typedef enum eRvkImageCapability {
  RvkImageCapability_None            = 0,
  RvkImageCapability_TransferSource  = 1 << 0,
  RvkImageCapability_TransferDest    = 1 << 1,
  RvkImageCapability_Sampled         = 1 << 2,
  RvkImageCapability_AttachmentColor = 1 << 3,
  RvkImageCapability_AttachmentDepth = 1 << 4,
  RvkImageCapability_Present         = 1 << 5,
} RvkImageCapability;

typedef struct sRvkImage {
  RvkImageType       type : 8;
  RvkImagePhase      phase : 8;
  RvkImageCapability caps : 8;
  VkFormat           vkFormat;
  RvkSize            size;
  u8                 layers;
  u8                 mipLevels;
  VkImage            vkImage;
  VkImageView        vkImageView;
  RvkMem             mem;
} RvkImage;

RvkImage rvk_image_create_source_color(RvkDevice*, VkFormat, RvkSize, u8 layers, u8 mipLevels);
RvkImage rvk_image_create_source_color_cube(RvkDevice*, VkFormat, RvkSize, u8 mipLevels);
RvkImage rvk_image_create_attach_color(RvkDevice*, VkFormat, RvkSize, RvkImageCapability extraCaps);
RvkImage rvk_image_create_attach_depth(RvkDevice*, VkFormat, RvkSize, RvkImageCapability extraCaps);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RvkSize);
void     rvk_image_destroy(RvkImage*, RvkDevice*);

String rvk_image_type_str(RvkImageType);
String rvk_image_phase_str(RvkImagePhase);

void rvk_image_assert_phase(const RvkImage*, RvkImagePhase);
void rvk_image_transition(RvkImage*, RvkImagePhase, VkCommandBuffer);
void rvk_image_transition_external(RvkImage*, RvkImagePhase);

void rvk_image_generate_mipmaps(RvkImage*, VkCommandBuffer);
void rvk_image_clear_color(const RvkImage*, GeoColor, VkCommandBuffer);
void rvk_image_clear_depth(const RvkImage*, f32 depth, VkCommandBuffer);
void rvk_image_copy(const RvkImage* src, RvkImage* dest, VkCommandBuffer);
void rvk_image_blit(const RvkImage* src, RvkImage* dest, VkCommandBuffer);

void rvk_image_transfer_ownership(
    const RvkImage*,
    VkCommandBuffer srcCmdBuf,
    VkCommandBuffer dstCmdBuf,
    u32             srcQueueFamIdx,
    u32             dstQueueFamIdx);
