#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "shader_internal.h"

#define VOLO_RVK_SHADER_LOGGING 0

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

MAYBE_UNUSED static String rvk_shader_kind_str(const AssetShaderKind kind) {
  static const String g_msgs[] = {
      string_static("SpvVertex"),
      string_static("SpvFragment"),
  };
  ASSERT(array_elems(g_msgs) == AssetShaderKind_Count, "Incorrect number of shader-kind names");
  return g_msgs[kind];
}

static RvkDescKind rvk_shader_desc_kind(const AssetShaderResKind resKind) {
  switch (resKind) {
  case AssetShaderResKind_Texture2D:
    return RvkDescKind_CombinedImageSampler2D;
  case AssetShaderResKind_TextureCube:
    return RvkDescKind_CombinedImageSamplerCube;
  case AssetShaderResKind_UniformBuffer:
    return RvkDescKind_UniformBuffer;
  case AssetShaderResKind_StorageBuffer:
    return RvkDescKind_StorageBuffer;
  case AssetShaderResKind_Count:
    break;
  }
  diag_crash();
}

static bool rvk_shader_spec_type(const RvkShader* shader, const u8 binding, AssetShaderType* out) {
  heap_array_for_t(shader->specs, AssetShaderSpec, spec) {
    if (spec->binding == binding) {
      *out = (AssetShaderType)spec->type;
      return true;
    }
  }
  return false;
}

static AssetShaderSpecDef rvk_shader_spec_default(const RvkShader* shader, const u8 binding) {
  heap_array_for_t(shader->specs, AssetShaderSpec, spec) {
    if (spec->binding == binding) {
      return (AssetShaderSpecDef)spec->defVal;
    }
  }
  return AssetShaderSpecDef_Other;
}

static usize rvk_shader_spec_size(const AssetShaderType type) {
  static const usize g_sizes[] = {
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
  ASSERT(array_elems(g_sizes) == AssetShaderType_Count, "Incorrect number of shader-type sizes");
  return g_sizes[type];
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

static RvkShaderFlags rvk_shader_flags(const AssetShaderComp* asset) {
  RvkShaderFlags flags = 0;
  if (asset->flags & AssetShaderFlags_MayKill) {
    flags |= RvkShaderFlags_MayKill;
  }
  return flags;
}

RvkShader* rvk_shader_create(RvkDevice* dev, const AssetShaderComp* asset, const String dbgName) {
  RvkShader* shader = alloc_alloc_t(g_allocHeap, RvkShader);

  *shader = (RvkShader){
      .vkModule          = rvk_shader_module_create(dev, asset),
      .vkStage           = rvk_shader_stage(asset->kind),
      .flags             = rvk_shader_flags(asset),
      .killSpecConstMask = asset->killSpecConstMask,
      .entryPoint        = string_dup(g_allocHeap, asset->entryPoint),
      .inputMask         = asset->inputMask,
      .outputMask        = asset->outputMask,
  };

  if (shader->flags & RvkShaderFlags_MayKill && asset->kind != AssetShaderKind_SpvFragment) {
    log_e("Non-fragment shader uses kill", log_param("shader", fmt_text(dbgName)));
  }

  rvk_debug_name_shader(dev->debug, shader->vkModule, "{}", fmt_text(dbgName));

  // Copy the specialization bindings.
  if (asset->specs.count) {
    shader->specs.values = alloc_array_t(g_allocHeap, AssetShaderSpec, asset->specs.count);
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

#if VOLO_RVK_SHADER_LOGGING
  log_d(
      "Vulkan shader created",
      log_param("name", fmt_text(dbgName)),
      log_param("kind", fmt_text(rvk_shader_kind_str(asset->kind))),
      log_param("entry", fmt_text(asset->entryPoint)),
      log_param("resources", fmt_int(asset->resources.count)),
      log_param("specs", fmt_int(asset->specs.count)));
#endif

  return shader;
}

void rvk_shader_destroy(RvkShader* shader, RvkDevice* dev) {
  vkDestroyShaderModule(dev->vkDev, shader->vkModule, &dev->vkAlloc);
  string_free(g_allocHeap, shader->entryPoint);

  if (shader->specs.values) {
    alloc_free_array_t(g_allocHeap, shader->specs.values, shader->specs.count);
  }

#if VOLO_RVK_SHADER_LOGGING
  log_d("Vulkan shader destroyed");
#endif

  alloc_free_t(g_allocHeap, shader);
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

bool rvk_shader_may_kill(
    const RvkShader* shader, const RvkShaderOverride* overrides, const usize overrideCount) {

  if (!(shader->flags & RvkShaderFlags_MayKill)) {
    return false; // Shader has no kill instruction at all.
  }

  // Check if any required 'true' spec constant is actually 'false'.
  const BitSet requiredTrueSpecConsts = bitset_from_var(shader->killSpecConstMask);
  bitset_for(requiredTrueSpecConsts, requiredTrueSpecBinding) {
    // Check if we have an override for the given spec binding.
    for (u32 overrideIdx = 0; overrideIdx != overrideCount; ++overrideIdx) {
      if (overrides[overrideIdx].binding != requiredTrueSpecBinding) {
        continue; // Override is for a different specialization constant binding.
      }
      if (overrides[overrideIdx].value == 0) {
        return false; // Required constant is 'false' meaning kill instruction cannot be reached.
      }
      goto NextRequiredSpecConstant;
    }

    // No override: Check if the default is 'false'.
    if (rvk_shader_spec_default(shader, (u8)requiredTrueSpecBinding) == AssetShaderSpecDef_False) {
      return false; // Required constant is 'false' meaning kill instruction cannot be reached.
    }

  NextRequiredSpecConstant:;
  }

  return true; // Kill instruction may be reachable.
}

VkSpecializationInfo rvk_shader_specialize_scratch(
    const RvkShader* shader, const RvkShaderOverride* overrides, const usize overrideCount) {

  enum {
    Limit_EntriesMax  = 64,
    Limit_TypeSizeMax = 8,
    Limit_DataSizeMax = Limit_EntriesMax * Limit_TypeSizeMax,
  };

  if (UNLIKELY(overrideCount > Limit_EntriesMax)) {
    log_e(
        "More then {} shader overrides are not supported",
        log_param("limit", fmt_int(Limit_EntriesMax)),
        log_param("provided", fmt_int(overrideCount)));
  }

  VkSpecializationMapEntry* entries =
      alloc_array_t(g_allocScratch, VkSpecializationMapEntry, Limit_EntriesMax);
  u32       entryCount       = 0;
  u64       usedBindingsMask = 0;
  const Mem buffer           = alloc_alloc(g_allocScratch, Limit_DataSizeMax, 8);
  Mem       remainingBuffer  = buffer;

  for (usize i = 0; i != math_min(overrideCount, Limit_EntriesMax); ++i) {
    const RvkShaderOverride* override = &overrides[i];

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
