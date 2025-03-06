#pragma once
#include "asset_graphic.h"
#include "vulkan_api.h"

#include "desc_internal.h"
#include "forward_internal.h"
#include "sampler_internal.h"

enum {
  RvkGraphicSet_Global   = 0,
  RvkGraphicSet_Draw     = 1,
  RvkGraphicSet_Graphic  = 2,
  RvkGraphicSet_Instance = 3,
};

#define rvk_graphic_shaders_max 2
#define rvk_graphic_samplers_max 6

typedef enum {
  RvkGraphicFlags_MayDiscard         = 1 << 0, // Graphic might discard a fragment.
  RvkGraphicFlags_RequireDrawSet     = 1 << 1,
  RvkGraphicFlags_RequireInstanceSet = 1 << 2,
  RvkGraphicFlags_Invalid            = 1 << 3,
} RvkGraphicFlags;

typedef struct sRvkGraphic {
  String            dbgName;
  i32               passOrder;
  AssetGraphicPass  passId : 8;
  u8                samplerMask;
  RvkGraphicFlags   flags : 16;
  u16               globalBindings;
  u32               vertexCount;
  const RvkShader*  shaders[rvk_graphic_shaders_max];
  const RvkMesh*    mesh;
  const RvkTexture* samplerTextures[rvk_graphic_samplers_max];
  RvkSamplerSpec    samplerSpecs[rvk_graphic_samplers_max];
  RvkDescSet        graphicDescSet;
  RvkDescMeta       drawDescMeta;
  VkPipelineLayout  vkPipelineLayout;
  VkPipeline        vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*, String dbgName);
void        rvk_graphic_destroy(RvkGraphic*, RvkDevice*);

void rvk_graphic_add_shader(RvkGraphic*, const RvkShader*);
void rvk_graphic_add_mesh(RvkGraphic*, const RvkMesh*);

void rvk_graphic_add_sampler(
    RvkGraphic*, const AssetGraphicComp*, const RvkTexture*, const AssetGraphicSampler*);

bool rvk_graphic_finalize(RvkGraphic*, const AssetGraphicComp*, RvkDevice*, const RvkPass*);

bool rvk_graphic_is_ready(const RvkGraphic*, const RvkDevice*);
void rvk_graphic_bind(const RvkGraphic*, const RvkDevice*, const RvkPass*, VkCommandBuffer);
