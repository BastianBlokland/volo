#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"
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
  case AssetShaderKind_Count:
    break;
  }
  diag_crash();
}

static String rvk_shader_kind_str(const AssetShaderKind kind) {
  static const String msgs[] = {
      string_static("SpvVertex"),
      string_static("SpvFragment"),
  };
  ASSERT(array_elems(msgs) == AssetShaderKind_Count, "Incorrect number of shader-kind names");
  return msgs[kind];
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
  case AssetShaderResKind_Count:
    break;
  }
  diag_crash();
}

RvkShader* rvk_shader_create(RvkDevice* dev, const AssetShaderComp* asset, const String dbgName) {
  RvkShader* shader = alloc_alloc_t(g_alloc_heap, RvkShader);
  *shader           = (RvkShader){
      .device     = dev,
      .vkModule   = rvk_shader_module_create(dev, asset),
      .vkStage    = rvk_shader_stage(asset->kind),
      .entryPoint = string_dup(g_alloc_heap, asset->entryPoint),
  };
  rvk_debug_name_shader(dev->debug, shader->vkModule, "{}", fmt_text(dbgName));

  for (usize i = 0; i != asset->resourceCount; ++i) {
    const AssetShaderRes* res = &asset->resources[i];
    if (res->set >= rvk_shader_desc_max) {
      diag_crash_msg("Shader resource set {} is out of bounds", fmt_int(res->set));
    }
    if (res->binding >= rvk_desc_bindings_max) {
      diag_crash_msg("Shader resource binding {} is out of bounds", fmt_int(res->binding));
    }
    shader->descriptors[res->set].bindings[res->binding] = rvk_shader_desc_kind(res->kind);
  }

  log_d(
      "Vulkan shader created",
      log_param("name", fmt_text(dbgName)),
      log_param("kind", fmt_text(rvk_shader_kind_str(asset->kind))),
      log_param("entry", fmt_text(asset->entryPoint)),
      log_param("resources", fmt_int(asset->resourceCount)));
  return shader;
}

void rvk_shader_destroy(RvkShader* shader) {
  RvkDevice* dev = shader->device;

  vkDestroyShaderModule(dev->vkDev, shader->vkModule, &dev->vkAlloc);
  string_free(g_alloc_heap, shader->entryPoint);

  alloc_free_t(g_alloc_heap, shader);
}
