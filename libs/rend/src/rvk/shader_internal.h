#pragma once
#include "asset_shader.h"

#include "desc_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

#define rvk_shader_desc_max 5

typedef struct sRvkShader {
  RvkDevice*            dev;
  VkShaderStageFlagBits vkStage;
  VkShaderModule        vkModule;
  String                entryPointName;
  RvkDescMeta           descriptors[rvk_shader_desc_max];
} RvkShader;

RvkShader rvk_shader_create(RvkDevice*, const AssetShaderComp*);
void      rvk_shader_destroy(RvkShader*);
