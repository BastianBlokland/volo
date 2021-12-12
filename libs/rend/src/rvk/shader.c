#include "core_alloc.h"
#include "core_diag.h"

#include "shader_internal.h"

static VkShaderModule rvk_shader_module_create(RvkDevice* dev, const AssetShaderComp* asset) {
  const VkShaderModuleCreateInfo createInfo = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = asset->data.size,
      .pCode    = (const u32*)asset->data.ptr,
  };
  VkShaderModule result;
  rvk_call(vkCreateShaderModule, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkShaderStageFlagBits rvk_shader_stage(const AssetShaderKind kind) {
  switch (kind) {
  case AssetShaderKind_SpvVertex:
    return VK_SHADER_STAGE_VERTEX_BIT;
  case AssetShaderKind_SpvFragment:
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  diag_crash();
}

static RvkDescKind rvk_shader_desc_kind(const AssetShaderResKind resKind) {
  switch (resKind) {
  case AssetShaderResKind_Texture:
    return RvkDescKind_CombinedImageSampler;
  case AssetShaderResKind_UniformBuffer:
    // NOTE: This makes the assumption that all uniform-buffers will be bound as dynamic buffers.
    return RvkDescKind_UniformBufferDynamic;
  case AssetShaderResKind_StorageBuffer:
    return RvkDescKind_StorageBuffer;
  }
  diag_crash();
}

RvkShader rvk_shader_create(RvkDevice* dev, const AssetShaderComp* asset) {
  RvkShader result = {
      .dev            = dev,
      .vkModule       = rvk_shader_module_create(dev, asset),
      .vkStage        = rvk_shader_stage(asset->kind),
      .entryPointName = string_dup(g_alloc_heap, asset->entryPointName),
  };

  for (usize i = 0; i != asset->resourceCount; ++i) {
    const AssetShaderRes* res = &asset->resources[i];
    if (res->set >= rvk_shader_desc_max) {
      diag_crash_msg("Shader resource set {} is out of bounds", fmt_int(res->set));
    }
    if (res->binding >= rvk_desc_bindings_max) {
      diag_crash_msg("Shader resource binding {} is out of bounds", fmt_int(res->binding));
    }
    result.descriptors[res->set].bindings[res->binding] = rvk_shader_desc_kind(res->kind);
  }

  return result;
}

void rvk_shader_destroy(RvkShader* shader) {
  vkDestroyShaderModule(shader->dev->vkDev, shader->vkModule, &shader->dev->vkAlloc);
  string_free(g_alloc_heap, shader->entryPointName);
}
