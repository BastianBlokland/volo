#pragma once
#include "asset_shader.h"

#include "desc_internal.h"

// Forward declare from 'asset_graphic.h'.
typedef struct sAssetGraphicOverride AssetGraphicOverride;

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

#define rvk_shader_desc_max 5

typedef enum {
  RvkShaderFlags_MayKill = 1 << 0, // Shader might kill (aka discard) the invocation.
} RvkShaderFlags;

typedef struct sRvkShader {
  VkShaderStageFlagBits vkStage;
  VkShaderModule        vkModule;
  String                entryPoint;
  RvkShaderFlags        flags : 8;
  u16                   killSpecConstMask; // Mask of spec-consts that need to be true for kill.
  RvkDescMeta           descriptors[rvk_shader_desc_max];
  u16                   inputMask, outputMask;
  HeapArray_t(AssetShaderSpec) specs;
} RvkShader;

RvkShader* rvk_shader_create(RvkDevice*, const AssetShaderComp*, String dbgName);
void       rvk_shader_destroy(RvkShader*, RvkDevice*);

bool rvk_shader_set_used(const RvkShader*, u32 set);
bool rvk_shader_may_kill(
    const RvkShader*, const AssetGraphicOverride* overrides, usize overrideCount);

/**
 * Create a 'VkSpecializationInfo' structure for specializing this shader with the given overrides.
 * NOTE: Specialization constants are written to scratch memory; meaning this specialization should
 * be used immediately and not be stored.
 */
VkSpecializationInfo rvk_shader_specialize_scratch(
    const RvkShader*, const AssetGraphicOverride* overrides, usize overrideCount);
