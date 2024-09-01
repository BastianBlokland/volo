#pragma once
#include "core_time.h"

#include "sampler_internal.h"
#include "types_internal.h"

#define rvk_pass_attach_color_max 2

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
typedef struct sRvkTexture     RvkTexture;
typedef struct sRvkUniformPool RvkUniformPool;

typedef struct sRvkPass RvkPass;

typedef enum {
  RvkPassLoad_DontCare = 0,
  RvkPassLoad_Clear,
  RvkPassLoad_Preserve,
  RvkPassLoad_PreserveDontCheck, // Preserve but don't validate contents, here be dragons.
} RvkPassLoad;

typedef enum {
  RvkPassDepth_None,      // No depth attachment, depth testing will not be available.
  RvkPassDepth_Transient, // Transient depth attachment, can only be used during this pass.
  RvkPassDepth_Stored,    // Stored depth attachment, can be sampled by other passes later.
} RvkPassDepth;

typedef enum {
  RvkPassFormat_None = 0,
  RvkPassFormat_Color1Linear,      // R    (unorm)  sdr linear.
  RvkPassFormat_Color2Linear,      // RG   (unorm)  sdr linear.
  RvkPassFormat_Color4Linear,      // RGBA (unorm)  sdr linear.
  RvkPassFormat_Color4Srgb,        // RGBA (unorm)  sdr srgb.
  RvkPassFormat_Color2SignedFloat, // RG   (sfloat) hdr.
  RvkPassFormat_Color3Float,       // RGB  (ufloat) hdr.
  RvkPassFormat_Swapchain,         // Format matching the window's swapchain.
} RvkPassFormat;

typedef struct sRvkPassConfig {
  String        name; // Needs to be persistently allocated.
  RvkPassDepth  attachDepth : 8;
  RvkPassLoad   attachDepthLoad : 8;
  RvkPassFormat attachColorFormat[rvk_pass_attach_color_max];
  RvkPassLoad   attachColorLoad[rvk_pass_attach_color_max];
} RvkPassConfig;

typedef struct sRvkPassDraw {
  RvkGraphic*    graphic;
  Mem            instData;
  Mem            drawData;    // Per-draw data to use.
  const RvkMesh* drawMesh;    // Per-draw mesh to use.
  RvkImage*      drawImage;   // Per-draw image to use.
  RvkSamplerSpec drawSampler; // Sampler specification for a per-draw image.
  u32            vertexCountOverride;
  u32            instCount;
  u32            instDataStride;
} RvkPassDraw;

RvkPass* rvk_pass_create(
    RvkDevice*,
    VkFormat swapchainFormat,
    VkCommandBuffer,
    RvkUniformPool*,
    RvkStopwatch*,
    RvkPassConfig);
void   rvk_pass_destroy(RvkPass*);
bool   rvk_pass_active(const RvkPass*);
String rvk_pass_name(const RvkPass*); // Persistently allocated.
bool   rvk_pass_has_depth(const RvkPass*);

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass*, u16 colorAttachIndex);
RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass*);
RvkDescMeta   rvk_pass_meta_global(const RvkPass*);
RvkDescMeta   rvk_pass_meta_instance(const RvkPass*);
VkRenderPass  rvk_pass_vkrenderpass(const RvkPass*);

u16          rvk_pass_stat_invocations(const RvkPass*);
u16          rvk_pass_stat_draws(const RvkPass*);
u32          rvk_pass_stat_instances(const RvkPass*);
RvkSize      rvk_pass_stat_size_max(const RvkPass*);
TimeDuration rvk_pass_stat_duration(const RvkPass*);
u64          rvk_pass_stat_pipeline(const RvkPass*, RvkStat);

void rvk_pass_reset(RvkPass*);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);
bool rvk_pass_prepare_mesh(RvkPass*, const RvkMesh*);
bool rvk_pass_prepare_texture(RvkPass*, RvkTexture*);

void rvk_pass_stage_clear_color(RvkPass*, GeoColor clearColor);
void rvk_pass_stage_attach_color(RvkPass*, RvkImage*, u16 colorAttachIndex);
void rvk_pass_stage_attach_depth(RvkPass*, RvkImage*);
void rvk_pass_stage_global_data(RvkPass*, Mem, u16 dataIndex);
void rvk_pass_stage_global_image(RvkPass*, RvkImage*, u16 imageIndex);
void rvk_pass_stage_global_shadow(RvkPass*, RvkImage*, u16 imageIndex);
void rvk_pass_stage_draw_image(RvkPass*, RvkImage*);

void rvk_pass_begin(RvkPass*);
void rvk_pass_draw(RvkPass*, const RvkPassDraw*);
void rvk_pass_end(RvkPass*);
void rvk_pass_discard(RvkPass*); // Discard any staged values.
