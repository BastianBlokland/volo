#pragma once
#include "core_types.h"
#include "rend_color.h"
#include "rend_size.h"

#include "statrecorder_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkUniformPool RvkUniformPool;

typedef struct sRvkPass RvkPass;

typedef enum {
  RvkPassFlags_ClearColor = 1 << 0,
  RvkPassFlags_ClearDepth = 1 << 1,

  RvkPassFlags_Default = RvkPassFlags_ClearDepth,

  RvkPassFlags_Count = 2,
} RvkPassFlags;

typedef struct {
  RvkGraphic* graphic;
  u32         instanceCount;
  Mem         data;
  u32         dataStride;
} RvkPassDraw;

typedef struct {
  RvkPassDraw* values;
  usize        count;
} RvkPassDrawList;

RvkPass* rvk_pass_create(RvkDevice*, VkCommandBuffer, RvkUniformPool*, RvkPassFlags);
void     rvk_pass_destroy(RvkPass*);
bool     rvk_pass_active(const RvkPass*);

RvkImage* rvk_pass_output(RvkPass*);
u64       rvk_pass_stat(RvkPass*, RvkStat);

void rvk_pass_setup(RvkPass*, RendSize size);
bool rvk_pass_prepare(RvkPass*, RvkGraphic*);

void rvk_pass_begin(RvkPass*, RendColor clearColor);
void rvk_pass_draw(RvkPass*, Mem globalData, RvkPassDrawList);
void rvk_pass_end(RvkPass*);
