#pragma once
#include "core_time.h"
#include "geo_color.h"

#include "statrecorder_internal.h"
#include "types_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDescMeta    RvkDescMeta;
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkMesh        RvkMesh;
typedef struct sRvkStopwatch   RvkStopwatch;
typedef struct sRvkUniformPool RvkUniformPool;

typedef struct sRvkPass RvkPass;

typedef enum {
  RvkPassFlags_None          = 0,
  RvkPassFlags_ClearColor    = 1 << 0,
  RvkPassFlags_ClearDepth    = 1 << 1,
  RvkPassFlags_Color1        = 1 << 2, // Enable a color attachment.
  RvkPassFlags_Color2        = 1 << 3, // Enable a second color attachment.
  RvkPassFlags_DepthOutput   = 1 << 4, // Support outputting the depth attachment.
  RvkPassFlags_ExternalDepth = 1 << 5, // Call 'rvk_pass_use_depth()' with a source depth image.

  RvkPassFlags_Clear = RvkPassFlags_ClearColor | RvkPassFlags_ClearDepth,

  RvkPassFlags_Count = 6,
} RvkPassFlags;

typedef enum {
  RvkPassOutput_Color1,
  RvkPassOutput_Color2,
  RvkPassOutput_Depth,

  RvkPassOutput_Count,
} RvkPassOutput;

typedef struct sRvkPassDraw {
  RvkGraphic* graphic;
  RvkMesh*    dynMesh; // Dynamic (late bound) mesh to use in this draw.
  u32         vertexCountOverride;
  Mem         drawData;
  u32         instCount;
  Mem         instData;
  u32         instDataStride;
} RvkPassDraw;

RvkPass* rvk_pass_create(
    RvkDevice*, VkCommandBuffer, RvkUniformPool*, RvkStopwatch*, RvkPassFlags, String name);
void    rvk_pass_destroy(RvkPass*);
bool    rvk_pass_active(const RvkPass*);
String  rvk_pass_name(const RvkPass*);
RvkSize rvk_pass_size(const RvkPass*);
bool    rvk_pass_recorded(const RvkPass*);

RvkDescMeta  rvk_pass_meta_global(const RvkPass*);
RvkDescMeta  rvk_pass_meta_dynamic(const RvkPass*);
RvkDescMeta  rvk_pass_meta_draw(const RvkPass*);
RvkDescMeta  rvk_pass_meta_instance(const RvkPass*);
VkRenderPass rvk_pass_vkrenderpass(const RvkPass*);

RvkImage*    rvk_pass_output(RvkPass*, RvkPassOutput);
u64          rvk_pass_stat(const RvkPass*, RvkStat);
TimeDuration rvk_pass_duration(const RvkPass*);

void rvk_pass_setup(RvkPass*, RvkSize size);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);
bool rvk_pass_prepare_mesh(RvkPass*, RvkMesh*);

void rvk_pass_use_depth(RvkPass*, RvkImage*);

void rvk_pass_bind_global_data(RvkPass*, Mem);
void rvk_pass_bind_global_image(RvkPass*, RvkImage*, u16 imageIndex);

void rvk_pass_begin(RvkPass*, GeoColor clearColor);
void rvk_pass_draw(RvkPass*, const RvkPassDraw*);
void rvk_pass_end(RvkPass*);
