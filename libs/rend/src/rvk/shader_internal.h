#pragma once
#include "asset_shader.h"

#include "desc_internal.h"

// Internal forward declarations:
typedef struct sRvkPlatform RvkPlatform;

#define rvk_shader_desc_max 5

typedef struct sRvkShader {
  RvkPlatform*          platform;
  VkShaderStageFlagBits vkStage;
  VkShaderModule        vkModule;
  String                entryPoint;
  RvkDescMeta           descriptors[rvk_shader_desc_max];
} RvkShader;

RvkShader* rvk_shader_create(RvkPlatform*, const AssetShaderComp*);
void       rvk_shader_destroy(RvkShader*);
