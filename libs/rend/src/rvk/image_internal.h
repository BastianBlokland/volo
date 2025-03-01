#pragma once
#include "geo.h"

#include "forward_internal.h"
#include "mem_internal.h"
#include "types_internal.h"

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
  RvkImageCapability_BlitDest        = 1 << 2,
  RvkImageCapability_Sampled         = 1 << 3,
  RvkImageCapability_AttachmentColor = 1 << 4,
  RvkImageCapability_AttachmentDepth = 1 << 5,
  RvkImageCapability_Present         = 1 << 6,
} RvkImageCapability;

typedef struct sRvkImage {
  RvkImageType       type : 8;
  RvkImagePhase      phase : 8;
  RvkImageCapability caps : 8;
  u8                 layers;
  u8                 mipLevels;
  bool               frozen;
  VkFormat           vkFormat;
  RvkSize            size;
  VkImage            vkImage;
  VkImageView        vkImageView;
  RvkMem             mem;
} RvkImage;

// clang-format off

RvkImage rvk_image_create_source_color(RvkDevice*, VkFormat, RvkSize, u8 layers, u8 mipLevels, bool mipGpuGen);
RvkImage rvk_image_create_source_color_cube(RvkDevice*, VkFormat, RvkSize, u8 mipLevels, bool mipGpuGen);
RvkImage rvk_image_create_attach_color(RvkDevice*, VkFormat, RvkSize, RvkImageCapability extraCaps);
RvkImage rvk_image_create_attach_depth(RvkDevice*, VkFormat, RvkSize, RvkImageCapability extraCaps);
RvkImage rvk_image_create_swapchain(RvkDevice*, VkImage, VkFormat, RvkSize);
void     rvk_image_destroy(RvkImage*, RvkDevice*);

// clang-format on

String rvk_image_type_str(RvkImageType);
String rvk_image_phase_str(RvkImagePhase);

void rvk_image_assert_phase(const RvkImage*, RvkImagePhase);

void rvk_image_freeze(RvkImage*);

typedef struct {
  RvkImage*     img;
  RvkImagePhase phase;
} RvkImageTransition;

void rvk_image_transition(RvkDevice*, RvkImage*, RvkImagePhase, VkCommandBuffer);
void rvk_image_transition_batch(RvkDevice*, const RvkImageTransition*, u32 count, VkCommandBuffer);
void rvk_image_transition_external(RvkImage*, RvkImagePhase);

void rvk_image_generate_mipmaps(RvkDevice*, RvkImage*, VkCommandBuffer);
void rvk_image_clear_color(RvkDevice*, const RvkImage*, GeoColor, VkCommandBuffer);
void rvk_image_clear_depth(RvkDevice*, const RvkImage*, f32 depth, VkCommandBuffer);
void rvk_image_copy(RvkDevice*, const RvkImage* src, RvkImage* dest, VkCommandBuffer);
void rvk_image_blit(RvkDevice*, const RvkImage* src, RvkImage* dest, VkCommandBuffer);

void rvk_image_transfer_ownership(
    RvkDevice*,
    const RvkImage*,
    VkCommandBuffer srcCmdBuf,
    VkCommandBuffer dstCmdBuf,
    u32             srcQueueFamIdx,
    u32             dstQueueFamIdx);
