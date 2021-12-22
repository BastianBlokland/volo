#pragma once
#include "asset_graphic.h"

#include "desc_internal.h"
#include "sampler_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkMesh    RvkMesh;
typedef struct sRvkPass    RvkPass;
typedef struct sRvkShader  RvkShader;
typedef struct sRvkTexture RvkTexture;

#define rvk_graphic_shaders_max 2
#define rvk_graphic_samplers_max 4

typedef struct {
  RvkTexture* texture;
  RvkSampler  sampler;
} RvkGraphicSampler;

typedef enum {
  RvkGraphicFlags_Ready = 1 << 0,
} RvkGraphicFlags;

typedef struct sRvkGraphic {
  RvkDevice*             device;
  RvkGraphicFlags        flags : 8;
  AssetGraphicTopology   topology : 8;
  AssetGraphicRasterizer rasterizer : 8;
  AssetGraphicBlend      blend : 8;
  AssetGraphicDepth      depth : 8;
  AssetGraphicCull       cull : 8;
  u32                    lineWidth;
  RvkShader*             shaders[rvk_graphic_shaders_max];
  RvkMesh*               mesh;
  RvkGraphicSampler      samplers[rvk_graphic_samplers_max];
  RvkDescSet             descSet;
  VkPipelineLayout       vkPipelineLayout;
  VkPipeline             vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*);
void        rvk_graphic_destroy(RvkGraphic*);
void        rvk_graphic_shader_add(RvkGraphic*, RvkShader*);
void        rvk_graphic_mesh_add(RvkGraphic*, RvkMesh*);
void        rvk_graphic_sampler_add(RvkGraphic*, RvkTexture*, const AssetGraphicSampler*);
u32         rvk_graphic_index_count(const RvkGraphic*);
bool        rvk_graphic_prepare(RvkGraphic*, VkCommandBuffer, VkRenderPass);
void        rvk_graphic_bind(const RvkGraphic*, VkCommandBuffer);
