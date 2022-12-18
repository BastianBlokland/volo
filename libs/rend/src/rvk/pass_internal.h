#pragma once
#include "core_types.h"
#include "geo_color.h"

#include "statrecorder_internal.h"
#include "types_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkMesh        RvkMesh;
typedef struct sRvkUniformPool RvkUniformPool;

typedef struct sRvkPass RvkPass;

typedef enum {
  RvkPassFlags_ClearColor = 1 << 0,
  RvkPassFlags_ClearDepth = 1 << 1,

  RvkPassFlags_Default = RvkPassFlags_ClearDepth,

  RvkPassFlags_Count = 2,
} RvkPassFlags;

typedef enum {
  RvkPassOutput_Color,

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

typedef struct sRvkPassDrawList {
  RvkPassDraw* values;
  usize        count;
} RvkPassDrawList;

RvkPass* rvk_pass_create(RvkDevice*, VkCommandBuffer, RvkUniformPool*, RvkPassFlags, String name);
void     rvk_pass_destroy(RvkPass*);
bool     rvk_pass_active(const RvkPass*);

RvkImage* rvk_pass_output(RvkPass*, RvkPassOutput);
u64       rvk_pass_stat(RvkPass*, RvkStat);

void rvk_pass_setup(RvkPass*, RvkSize size);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);
bool rvk_pass_prepare_mesh(RvkPass*, RvkMesh*);

void rvk_pass_begin(RvkPass*, GeoColor clearColor);
void rvk_pass_draw(RvkPass*, Mem globalData, RvkPassDrawList);
void rvk_pass_end(RvkPass*);
