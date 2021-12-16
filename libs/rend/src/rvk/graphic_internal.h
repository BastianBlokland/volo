#pragma once
#include "asset_graphic.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkShader RvkShader;

#define rvk_graphic_shaders_max 2

typedef struct sRvkGraphic {
  RvkDevice*             dev;
  AssetGraphicTopology   topology;
  AssetGraphicRasterizer rasterizer;
  u32                    lineWidth;
  AssetGraphicBlend      blend;
  AssetGraphicDepth      depth;
  AssetGraphicCull       cull;
  RvkShader*             shaders[rvk_graphic_shaders_max];
  VkPipelineLayout       vkPipelineLayout;
  VkPipeline             vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*);
void        rvk_graphic_destroy(RvkGraphic*);
void        rvk_graphic_shader_add(RvkGraphic*, RvkShader*);
bool        rvk_graphic_prepare(RvkGraphic*, const RvkCanvas*);
