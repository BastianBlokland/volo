#pragma once
#include "asset_graphic.h"

#include "desc_internal.h"
#include "sampler_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkMesh           RvkMesh;
typedef struct sRvkPass           RvkPass;
typedef struct sRvkShader         RvkShader;
typedef struct sRvkShaderOverride RvkShaderOverride;
typedef struct sRvkTexture        RvkTexture;

enum {
  RvkGraphicSet_Global   = 0,
  RvkGraphicSet_Graphic  = 1,
  RvkGraphicSet_Draw     = 2,
  RvkGraphicSet_Instance = 3,
};

#define rvk_graphic_shaders_max 2
#define rvk_graphic_samplers_max 6

typedef enum {
  RvkGraphicFlags_MayDiscard         = 1 << 0, // Graphic might discard a fragment.
  RvkGraphicFlags_DepthClamp         = 1 << 1,
  RvkGraphicFlags_Ready              = 1 << 2,
  RvkGraphicFlags_RequireDrawSet     = 1 << 3,
  RvkGraphicFlags_RequireInstanceSet = 1 << 4,
  RvkGraphicFlags_Invalid            = 1 << 5,
} RvkGraphicFlags;

typedef struct {
  const RvkShader* shader;
  HeapArray_t(RvkShaderOverride) overrides;
} RvkGraphicShader;

typedef struct sRvkGraphic {
  String                 dbgName;
  AssetGraphicPass       pass;
  i32                    passOrder;
  RvkGraphicFlags        flags : 16;
  AssetGraphicTopology   topology : 8;
  AssetGraphicRasterizer rasterizer : 8;
  AssetGraphicBlend      blend : 8;    // Blend mode for the primary attachment.
  AssetGraphicBlend      blendAux : 8; // Blend mode for the other attachments.
  AssetGraphicDepth      depth : 8;
  AssetGraphicCull       cull : 8;
  u8                     samplerMask;
  u16                    globalBindings;
  u16                    outputMask;
  u16                    lineWidth;
  f32                    depthBiasConstant, depthBiasSlope;
  u32                    vertexCount;
  f32                    blendConstant;
  RvkGraphicShader       shaders[rvk_graphic_shaders_max];
  const RvkMesh*         mesh;
  const RvkTexture*      samplerTextures[rvk_graphic_samplers_max];
  RvkSamplerSpec         samplerSpecs[rvk_graphic_samplers_max];
  RvkDescSet             graphicDescSet;
  RvkDescMeta            drawDescMeta;
  VkPipelineLayout       vkPipelineLayout;
  VkPipeline             vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*, String dbgName);
void        rvk_graphic_destroy(RvkGraphic*, RvkDevice*);

void rvk_graphic_add_shader(
    RvkGraphic*,
    const AssetGraphicComp*,
    const RvkShader*,
    AssetGraphicOverride* overrides,
    usize                 overrideCount);

void rvk_graphic_add_mesh(RvkGraphic*, const AssetGraphicComp*, const RvkMesh*);

void rvk_graphic_add_sampler(
    RvkGraphic*, const AssetGraphicComp*, const RvkTexture*, const AssetGraphicSampler*);

bool rvk_graphic_finalize(RvkGraphic*, const AssetGraphicComp*, RvkDevice*, const RvkPass*);

bool rvk_graphic_is_ready(const RvkGraphic*, const RvkDevice*);
void rvk_graphic_bind(const RvkGraphic*, const RvkDevice*, VkCommandBuffer);
