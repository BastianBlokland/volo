#pragma once
#include "core_time.h"
#include "geo_color.h"

#include "sampler_internal.h"
#include "types_internal.h"
#include "uniform_internal.h"

#define rvk_pass_attach_color_max 2
#define rvk_pass_global_data_max 1
#define rvk_pass_global_image_max 5
#define rvk_pass_draw_image_max 5

// Internal forward declarations:
typedef enum eRvkJobPhase     RvkJobPhase;
typedef enum eRvkStat         RvkStat;
typedef struct sRvkAttachSpec RvkAttachSpec;
typedef struct sRvkDescMeta   RvkDescMeta;
typedef struct sRvkDevice     RvkDevice;
typedef struct sRvkGraphic    RvkGraphic;
typedef struct sRvkImage      RvkImage;
typedef struct sRvkJob        RvkJob;
typedef struct sRvkMesh       RvkMesh;

typedef u8 RvkPassHandle;

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
  RvkPassFormat_Swapchain,         // BGRA (unorm)  sdr srgb.
} RvkPassFormat;

typedef struct sRvkPassConfig {
  String        name; // Needs to be persistently allocated.
  i32           id;
  RvkPassDepth  attachDepth : 8;
  RvkPassLoad   attachDepthLoad : 8;
  RvkPassFormat attachColorFormat[rvk_pass_attach_color_max];
  RvkPassLoad   attachColorLoad[rvk_pass_attach_color_max];
} RvkPassConfig;

typedef struct sRvkPassSetup {
  GeoColor clearColor;

  // Attachments.
  RvkImage* attachColors[rvk_pass_attach_color_max];
  RvkImage* attachDepth;

  // Global resources.
  RvkUniformHandle globalData[rvk_pass_global_data_max];
  RvkImage*        globalImages[rvk_pass_global_image_max];
  RvkSamplerSpec   globalImageSamplers[rvk_pass_global_image_max];

  // Per-draw resources.
  RvkImage* drawImages[rvk_pass_draw_image_max];
} RvkPassSetup;

typedef struct sRvkPassDraw {
  const RvkGraphic* graphic;
  const RvkMesh*    drawMesh;       // Per-draw mesh to use.
  RvkUniformHandle  drawData;       // Per-draw data to use.
  RvkUniformHandle  instData;       // Chained uniform data for each batch.
  RvkSamplerSpec    drawSampler;    // Sampler specification for a per-draw image.
  u16               drawImageIndex; // Per-draw image to use.
  u16               instDataStride;
  u32               instCount;
  u32               vertexCountOverride;
} RvkPassDraw;

RvkPass* rvk_pass_create(RvkDevice*, const RvkPassConfig* /* Needs to be persistently allocated */);
void     rvk_pass_destroy(RvkPass*);

const RvkPassConfig* rvk_pass_config(const RvkPass*);
bool                 rvk_pass_active(const RvkPass*);

RvkAttachSpec rvk_pass_spec_attach_color(const RvkPass*, u16 colorAttachIndex);
RvkAttachSpec rvk_pass_spec_attach_depth(const RvkPass*);
RvkDescMeta   rvk_pass_meta_global(const RvkPass*);
RvkDescMeta   rvk_pass_meta_instance(const RvkPass*);
VkRenderPass  rvk_pass_vkrenderpass(const RvkPass*);

RvkPassHandle rvk_pass_frame_begin(RvkPass*, RvkJob*);
void          rvk_pass_frame_end(RvkPass*, RvkPassHandle);
void          rvk_pass_frame_release(RvkPass*, RvkPassHandle);

u16          rvk_pass_stat_invocations(const RvkPass*, RvkPassHandle);
u16          rvk_pass_stat_draws(const RvkPass*, RvkPassHandle);
u32          rvk_pass_stat_instances(const RvkPass*, RvkPassHandle);
RvkSize      rvk_pass_stat_size_max(const RvkPass*, RvkPassHandle);
TimeDuration rvk_pass_stat_duration(const RvkPass*, RvkPassHandle);
u64          rvk_pass_stat_pipeline(const RvkPass*, RvkPassHandle, RvkStat);

u32 rvk_pass_batch_size(RvkPass*, u32 instanceDataSize);

/**
 * NOTE: Pass-setup has to remain identical between begin and end.
 */
void rvk_pass_begin(RvkPass*, const RvkPassSetup*, RvkJobPhase);
void rvk_pass_draw(RvkPass*, const RvkPassSetup*, const RvkPassDraw*);
void rvk_pass_end(RvkPass*, const RvkPassSetup*);
