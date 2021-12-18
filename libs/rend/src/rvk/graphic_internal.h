#pragma once
#include "asset_graphic.h"

#include "desc_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkShader RvkShader;
typedef struct sRvkMesh   RvkMesh;

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
  RvkMesh*               mesh;
  RvkDescSet             descSet;
  VkPipelineLayout       vkPipelineLayout;
  VkPipeline             vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*);
void        rvk_graphic_destroy(RvkGraphic*);
void        rvk_graphic_shader_add(RvkGraphic*, RvkShader*);
void        rvk_graphic_mesh_add(RvkGraphic*, RvkMesh*);
u32         rvk_graphic_index_count(const RvkGraphic*);
bool        rvk_graphic_prepare(RvkGraphic*, const RvkCanvas*);
void        rvk_graphic_bind(const RvkGraphic*, VkCommandBuffer);
