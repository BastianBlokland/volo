#pragma once
#include "asset_shader.h"

#include "desc_internal.h"
#include "device_internal.h"

#define rvk_shader_desc_max 5

typedef struct {
  RvkDevice*            dev;
  VkShaderStageFlagBits vkStage;
  VkShaderModule        vkModule;
  String                entryPointName;
  RvkDescMeta           descriptors[rvk_shader_desc_max];
} RvkShader;

RvkShader rvk_shader_create(RvkDevice*, const AssetShaderComp*);
void      rvk_shader_destroy(RvkShader*);
