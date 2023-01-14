#pragma once
#include "core_time.h"

#include "types_internal.h"
#include "vulkan_internal.h"

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef enum eRvkStat          RvkStat;
typedef struct sRvkAttachSpec  RvkAttachSpec;
typedef struct sRvkDescMeta    RvkDescMeta;
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkMesh        RvkMesh;
typedef struct sRvkStopwatch   RvkStopwatch;
typedef struct sRvkUniformPool RvkUniformPool;

typedef struct sRvkPass RvkPass;

typedef enum eRvkPassFlags {
  RvkPassFlags_None              = 0,
  RvkPassFlags_Color1            = 1 << 0, // Enable the color1 attachment.
  RvkPassFlags_Color1Srgb        = 1 << 1, // Use an SRGB format for the color1 attachment.
  RvkPassFlags_Color1Float       = 1 << 2, // Use an float format for the color1 attachment.
  RvkPassFlags_Color1Single      = 1 << 3, // Use a single channel format for the color1 attachment.
  RvkPassFlags_Color1Swapchain   = 1 << 4, // Use a swapchain image for the color1 attachment.
  RvkPassFlags_Color2            = 1 << 5, // Enable the color2 attachment.
  RvkPassFlags_Color2Srgb        = 1 << 6, // Use an SRGB format for the color2 attachment.
  RvkPassFlags_Color2Single      = 1 << 7, // Use a single channel format for the color2 attachment.
  RvkPassFlags_ColorClear        = 1 << 8, // Clear the color attachment.
  RvkPassFlags_ColorLoadTransfer = 1 << 9, // Load the color from transferred color attachments.
  RvkPassFlags_Depth             = 1 << 10, // Enable a depth attachment.
  RvkPassFlags_DepthClear        = 1 << 11, // Clear the depth attachment.
  RvkPassFlags_DepthLoadTransfer = 1 << 12, // Load the depth from a transferred depth attachment.
  RvkPassFlags_DepthStore        = 1 << 13, // Store the depth attachment for use in a later pass.

  RvkPassFlags_Count = 14,
} RvkPassFlags;

typedef struct sRvkPassDraw {
  RvkGraphic* graphic;
  RvkMesh*    dynMesh; // Dynamic (late bound) mesh to use in this draw.
  Mem         drawData;
  Mem         instData;
  u32         vertexCountOverride;
  u32         instCount;
  u32         instDataStride;
} RvkPassDraw;

RvkPass* rvk_pass_create(
    RvkDevice*,
    VkFormat swapchainFormat,
    VkCommandBuffer,
    RvkUniformPool*,
    RvkStopwatch*,
    RvkPassFlags,
    String name);
void    rvk_pass_destroy(RvkPass*);
bool    rvk_pass_active(const RvkPass*);
String  rvk_pass_name(const RvkPass*);
RvkSize rvk_pass_size(const RvkPass*);
bool    rvk_pass_has_depth(const RvkPass*);

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass*, u16 colorAttachIndex);
RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass*);
RvkDescMeta   rvk_pass_meta_global(const RvkPass*);
RvkDescMeta   rvk_pass_meta_dynamic(const RvkPass*);
RvkDescMeta   rvk_pass_meta_draw(const RvkPass*);
RvkDescMeta   rvk_pass_meta_instance(const RvkPass*);
VkRenderPass  rvk_pass_vkrenderpass(const RvkPass*);

u64          rvk_pass_stat(const RvkPass*, RvkStat);
TimeDuration rvk_pass_duration(const RvkPass*);

void rvk_pass_reset(RvkPass*);
void rvk_pass_set_size(RvkPass*, RvkSize size);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);
bool rvk_pass_prepare_mesh(RvkPass*, RvkMesh*);

void rvk_pass_bind_attach_color(RvkPass*, RvkImage*, u16 colorAttachIndex);
void rvk_pass_bind_attach_depth(RvkPass*, RvkImage*);
void rvk_pass_bind_global_data(RvkPass*, Mem);
void rvk_pass_bind_global_image(RvkPass*, RvkImage*, u16 imageIndex);
void rvk_pass_bind_global_shadow(RvkPass*, RvkImage*, u16 imageIndex);

void rvk_pass_begin(RvkPass*, GeoColor clearColor);
void rvk_pass_draw(RvkPass*, const RvkPassDraw*);
void rvk_pass_end(RvkPass*);
