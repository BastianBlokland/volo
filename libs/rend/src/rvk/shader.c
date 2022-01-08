#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
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

static bool rvk_shader_spec_type(RvkShader* shader, const u32 binding, AssetShaderType* out) {
  array_ptr_for_t(shader->specs, AssetShaderSpec, spec) {
    if (spec->binding == binding) {
      *out = spec->type;
      return true;
    }
  }
  return false;
}

static usize rvk_shader_spec_size(const AssetShaderType type) {
  static const usize sizes[] = {
      [AssetShaderType_bool] = sizeof(VkBool32),
      [AssetShaderType_u8]   = 1,
      [AssetShaderType_i8]   = 1,
      [AssetShaderType_u16]  = 2,
      [AssetShaderType_i16]  = 2,
      [AssetShaderType_u32]  = 4,
      [AssetShaderType_i32]  = 4,
      [AssetShaderType_u64]  = 8,
      [AssetShaderType_i64]  = 8,
      [AssetShaderType_f16]  = 2,
      [AssetShaderType_f32]  = 4,
      [AssetShaderType_f64]  = 8,
  };
  ASSERT(array_elems(sizes) == AssetShaderType_Count, "Incorrect number of shader-type sizes");
  return sizes[type];
}

static Mem rvk_shader_spec_write(Mem output, const AssetShaderType type, const f64 value) {
#define WRITE_TYPE_AND_RET(_NAME_)                                                                 \
  case AssetShaderType_##_NAME_:                                                                   \
    *mem_as_t(output, _NAME_) = (_NAME_)value;                                                     \
    return mem_consume(output, sizeof(_NAME_))

  switch (type) {
    WRITE_TYPE_AND_RET(u8);
    WRITE_TYPE_AND_RET(i8);
    WRITE_TYPE_AND_RET(u16);
    WRITE_TYPE_AND_RET(i16);
    WRITE_TYPE_AND_RET(u32);
    WRITE_TYPE_AND_RET(i32);
    WRITE_TYPE_AND_RET(u64);
    WRITE_TYPE_AND_RET(i64);
    WRITE_TYPE_AND_RET(f16);
    WRITE_TYPE_AND_RET(f32);
    WRITE_TYPE_AND_RET(f64);
  case AssetShaderType_bool:
    *mem_as_t(output, VkBool32) = value != 0;
    return mem_consume(output, sizeof(VkBool32));
  case AssetShaderType_Count:
    break;
  }
#undef WRITE_TYPE
  diag_crash();
}

RvkShader* rvk_shader_create(RvkDevice* dev, const AssetShaderComp* asset, const String dbgName) {
  RvkShader* shader = alloc_alloc_t(g_alloc_heap, RvkShader);
  *shader           = (RvkShader){
      .device     = dev,
      .dbgName    = string_dup(g_alloc_heap, dbgName),
      .vkModule   = rvk_shader_module_create(dev, asset),
      .vkStage    = rvk_shader_stage(asset->kind),
      .entryPoint = string_dup(g_alloc_heap, asset->entryPoint),
  };
  rvk_debug_name_shader(dev->debug, shader->vkModule, "{}", fmt_text(dbgName));

  // Copy the specialization bindings.
  if (asset->specs.count) {
    shader->specs.values = alloc_array_t(g_alloc_heap, AssetShaderSpec, asset->specs.count);
    shader->specs.count  = asset->specs.count;
    mem_cpy(
        mem_from_to(shader->specs.values, shader->specs.values + shader->specs.count),
        mem_from_to(asset->specs.values, asset->specs.values + asset->specs.count));
  }

  for (usize i = 0; i != asset->resources.count; ++i) {
    const AssetShaderRes* res = &asset->resources.values[i];
    if (res->set >= rvk_shader_desc_max) {
      log_e("Shader resource set out of bounds", log_param("set", fmt_int(res->set)));
      continue;
    }
    if (res->binding >= rvk_desc_bindings_max) {
      log_e("Shader resource binding out of bounds", log_param("binding", fmt_int(res->binding)));
      continue;
    }
    shader->descriptors[res->set].bindings[res->binding] = rvk_shader_desc_kind(res->kind);
  }

  log_d(
      "Vulkan shader created",
      log_param("name", fmt_text(dbgName)),
      log_param("kind", fmt_text(rvk_shader_kind_str(asset->kind))),
      log_param("entry", fmt_text(asset->entryPoint)),
      log_param("resources", fmt_int(asset->resources.count)),
      log_param("specs", fmt_int(asset->specs.count)));
  return shader;
}

void rvk_shader_destroy(RvkShader* shader) {
  RvkDevice* dev = shader->device;

  vkDestroyShaderModule(dev->vkDev, shader->vkModule, &dev->vkAlloc);
  string_free(g_alloc_heap, shader->entryPoint);

  if (shader->specs.values) {
    alloc_free_array_t(g_alloc_heap, shader->specs.values, shader->specs.count);
  }

  log_d("Vulkan shader destroyed", log_param("name", fmt_text(shader->dbgName)));

  string_free(g_alloc_heap, shader->dbgName);
  alloc_free_t(g_alloc_heap, shader);
}

bool rvk_shader_set_used(const RvkShader* shader, const u32 set) {
  if (UNLIKELY(set >= rvk_shader_desc_max)) {
    return false;
  }
  const RvkDescMeta* setDesc = &shader->descriptors[set];
  array_for_t(setDesc->bindings, u8, binding) {
    if (*binding != RvkDescKind_None) {
      return true;
    }
  }
  return false;
}

VkSpecializationInfo rvk_shader_specialize_scratch(
    RvkShader* shader, RvkShaderOverride* overrides, usize overrideCount) {

  enum {
    Limit_EntriesMax  = 64,
    Limit_TypeSizeMax = 8,
    Limit_DataSizeMax = Limit_EntriesMax * Limit_TypeSizeMax,
  };

  if (UNLIKELY(overrideCount > Limit_EntriesMax)) {
    log_w(
        "More then {} shader overrides are not supported",
        log_param("limit", fmt_int(Limit_EntriesMax)),
        log_param("provided", fmt_int(overrideCount)));
  }

  VkSpecializationMapEntry* entries =
      alloc_array_t(g_alloc_scratch, VkSpecializationMapEntry, Limit_EntriesMax);
  u32       entryCount       = 0;
  u64       usedBindingsMask = 0;
  const Mem buffer           = alloc_alloc(g_alloc_scratch, Limit_DataSizeMax, 8);
  Mem       remainingBuffer  = buffer;

  for (usize i = 0; i != math_min(overrideCount, Limit_EntriesMax); ++i) {
    RvkShaderOverride* override = &overrides[i];

    AssetShaderType type;
    if (UNLIKELY(!rvk_shader_spec_type(shader, override->binding, &type))) {
      log_e(
          "No specialization found for override '{}'",
          log_param("name", fmt_text(override->name)),
          log_param("binding", fmt_int(override->binding)));
      continue;
    }
    if (UNLIKELY(override->binding >= sizeof(usedBindingsMask) * 8)) {
      log_e(
          "Binding for specialization override '{}' exceeds maximum",
          log_param("name", fmt_text(override->name)));
      continue;
    }
    if (UNLIKELY(usedBindingsMask & u64_lit(1) << override->binding)) {
      log_e(
          "Duplicate specialization override '{}'",
          log_param("name", fmt_text(override->name)),
          log_param("binding", fmt_int(override->binding)));
      continue;
    }
    usedBindingsMask |= u64_lit(1) << override->binding;

    entries[entryCount++] = (VkSpecializationMapEntry){
        .constantID = override->binding,
        .offset     = (u32)(buffer.size - remainingBuffer.size),
        .size       = rvk_shader_spec_size(type),
    };
    remainingBuffer = rvk_shader_spec_write(remainingBuffer, type, override->value);
  }
  return (VkSpecializationInfo){
      .pMapEntries   = entries,
      .mapEntryCount = entryCount,
      .dataSize      = buffer.size - remainingBuffer.size,
      .pData         = buffer.ptr,
  };
}
