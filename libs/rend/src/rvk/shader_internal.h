#pragma once
#include "asset_shader.h"

#include "desc_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

#define rvk_shader_desc_max 5

typedef struct sRvkShaderOverride {
  String name;
  u32    binding;
  f64    value;
} RvkShaderOverride;

typedef struct sRvkShader {
  RvkDevice*            device;
  VkShaderStageFlagBits vkStage;
  VkShaderModule        vkModule;
  String                entryPoint;
  RvkDescMeta           descriptors[rvk_shader_desc_max];
  struct {
    AssetShaderSpec* values;
    usize            count;
  } specs;
} RvkShader;

RvkShader* rvk_shader_create(RvkDevice*, const AssetShaderComp*, String dbgName);
void       rvk_shader_destroy(RvkShader*);

bool rvk_shader_set_used(const RvkShader*, u32 set);

/**
 * Create a 'VkSpecializationInfo' structure for specializing this shader with the given overrides.
 * NOTE: Specialization constants are written to scratch memory; meaning this specialization should
 * be used immediately and not be stored.
 */
VkSpecializationInfo
rvk_shader_specialize_scratch(RvkShader*, RvkShaderOverride* overrides, usize overrideCount);
