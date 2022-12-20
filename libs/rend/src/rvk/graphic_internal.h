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
  RvkGraphicSet_Dynamic  = 2,
  RvkGraphicSet_Draw     = 3,
  RvkGraphicSet_Instance = 4,
};

#define rvk_graphic_shaders_max 2
#define rvk_graphic_samplers_max 6

typedef struct {
  RvkTexture* texture;
  RvkSampler  sampler;
} RvkGraphicSampler;

typedef enum {
  RvkGraphicFlags_Ready               = 1 << 0,
  RvkGraphicFlags_RequireDynamicMesh  = 1 << 2,
  RvkGraphicFlags_RequireDrawData     = 1 << 3,
  RvkGraphicFlags_RequireInstanceData = 1 << 4,
  RvkGraphicFlags_Invalid             = 1 << 5,
} RvkGraphicFlags;

typedef struct {
  RvkShader* shader;
  struct {
    RvkShaderOverride* values;
    usize              count;
  } overrides;
} RvkGraphicShader;

typedef struct sRvkGraphic {
  RvkDevice*             device;
  String                 dbgName;
  RvkGraphicFlags        flags : 8;
  AssetGraphicTopology   topology : 8;
  AssetGraphicRasterizer rasterizer : 8;
  AssetGraphicBlend      blend : 8;
  AssetGraphicDepth      depth : 8;
  AssetGraphicCull       cull : 8;
  u16                    globalBindings;
  u16                    outputMask;
  u32                    lineWidth;
  f32                    depthBias;
  i32                    renderOrder;
  u32                    vertexCount;
  RvkGraphicShader       shaders[rvk_graphic_shaders_max];
  RvkMesh*               mesh;
  RvkGraphicSampler      samplers[rvk_graphic_samplers_max];
  RvkDescSet             descSet;
  VkPipelineLayout       vkPipelineLayout;
  VkPipeline             vkPipeline;
} RvkGraphic;

RvkGraphic* rvk_graphic_create(RvkDevice*, const AssetGraphicComp*, String dbgName);
void        rvk_graphic_destroy(RvkGraphic*);

void rvk_graphic_shader_add(
    RvkGraphic*, RvkShader*, AssetGraphicOverride* overrides, usize overrideCount);
void rvk_graphic_mesh_add(RvkGraphic*, RvkMesh*);
void rvk_graphic_sampler_add(RvkGraphic*, RvkTexture*, const AssetGraphicSampler*);

bool rvk_graphic_prepare(RvkGraphic*, VkCommandBuffer, const RvkPass*);
void rvk_graphic_bind(const RvkGraphic*, VkCommandBuffer);
