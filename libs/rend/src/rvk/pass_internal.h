#pragma once
#include "core/time.h"
#include "geo/color.h"

#include "forward_internal.h"
#include "sampler_internal.h"
#include "types_internal.h"
#include "uniform_internal.h"

#define rvk_pass_attach_color_max 4
#define rvk_pass_global_data_max 1
#define rvk_pass_global_image_max 7
#define rvk_pass_draw_image_max 16

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
  RvkPassFormat_Color1Linear,       // R    (unorm)  sdr linear.
  RvkPassFormat_Color2Linear,       // RG   (unorm)  sdr linear.
  RvkPassFormat_Color4Linear,       // RGBA (unorm)  sdr linear.
  RvkPassFormat_Color4Srgb,         // RGBA (unorm)  sdr srgb.
  RvkPassFormat_Color3LowPrecision, // RGB  (unorm)  sdr low precision.
  RvkPassFormat_Color2SignedFloat,  // RG   (sfloat) hdr.
  RvkPassFormat_Color3Float,        // RGB  (ufloat) hdr.
  RvkPassFormat_Swapchain,          // BGRA (unorm)  sdr srgb.
} RvkPassFormat;

typedef struct sRvkPassConfig {
  String        name; // Needs to be persistently allocated.
  u32           id;
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

typedef struct {
  u16          invocationCount;
  u16          drawCount;
  u32          instanceCount;
  TimeDuration duration;
  RvkSize      sizeMax;
} RvkPassStats;

typedef struct {
  TimeSteady gpuTimeBegin, gpuTimeEnd;
} RvkPassStatsInvoc;

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

void rvk_pass_stats(const RvkPass*, RvkPassHandle, RvkPassStats* out);
u64  rvk_pass_stats_pipeline(const RvkPass*, RvkPassHandle, RvkStat);
void rvk_pass_stats_invoc(const RvkPass*, RvkPassHandle, u16 invocIdx, RvkPassStatsInvoc* out);

u32 rvk_pass_batch_size(RvkPass*, u32 instanceDataSize);

/**
 * NOTE: Pass-setup has to remain identical between begin and end.
 */
void rvk_pass_begin(RvkPass*, const RvkPassSetup*);
void rvk_pass_draw(RvkPass*, const RvkPassSetup*, const RvkPassDraw[], u32 count);
void rvk_pass_end(RvkPass*, const RvkPassSetup*);
