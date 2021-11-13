#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"

#include "loader_shader_internal.h"

/**
 * Spir-V (Standard Portable Intermediate Representation 5)
 * Spec: https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html
 */

#define spv_magic 0x07230203

typedef enum {
  SpvOp_EntryPoint       = 15,
  SpvOp_TypeSampledImage = 27,
  SpvOp_TypeStruct       = 30,
  SpvOp_TypePointer      = 32,
  SpvOp_Variable         = 59,
  SpvOp_Decorate         = 71,
} SpvOp;

typedef enum {
  SpvDecoration_Binding       = 33,
  SpvDecoration_DescriptorSet = 34,
} SpvDecoration;

typedef enum {
  SpvStorageClass_UniformConstant = 0,
  SpvStorageClass_Uniform         = 2,
  SpvStorageClass_StorageBuffer   = 12,
} SpvStorageClass;

typedef enum {
  SpvExecutionModel_Vertex   = 0,
  SpvExecutionModel_Fragment = 4,
} SpvExecutionModel;

typedef struct {
  const u32* ptr;
  u32        size;
} SpvData;

typedef struct {
  u8 major, minor;
} SpvVersion;

typedef struct {
  u16 opCode, opSize;
} SpvInstructionHeader;

typedef enum {
  SpvIdKind_Unknown,
  SpvIdKind_Variable,
  SpvIdKind_TypePointer,
  SpvIdKind_TypeStruct,
  SpvIdKind_TypeSampledImage,
} SpvIdKind;

typedef enum {
  SpvIdFlags_HasSet     = 1 << 0,
  SpvIdFlags_HasBinding = 1 << 1,
} SpvIdFlags;

typedef struct {
  SpvIdKind       kind;
  SpvIdFlags      flags;
  u32             set, binding, typeId;
  SpvStorageClass storageClass;
} SpvId;

typedef struct {
  SpvExecutionModel execModel;
  String            entryPointName;
  SpvId*            ids;
  u32               idCount;
} SpvProgram;

typedef enum {
  SpvError_None = 0,
  SpvError_Malformed,
  SpvError_MalformedIdOutOfBounds,
  SpvError_MalformedDuplicateId,
  SpvError_MalformedResourceWithoutSetAndId,
  SpvError_MalformedDuplicateBinding,
  SpvError_UnsupportedVersion,
  SpvError_UnsupportedMultipleEntryPoints,
  SpvError_UnsupportedShaderResource,
  SpvError_UnsupportedSetExceedsMax,
  SpvError_UnsupportedBindingExceedsMax,

  SpvError_Count,
} SpvError;

static String spv_error_str(SpvError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed SpirV data"),
      string_static("SpirV id out of bounds"),
      string_static("Duplicate SpirV id"),
      string_static("SpirV shader resource without set and binding"),
      string_static("SpirV shader resource binding already used in this set"),
      string_static("Unsupported SpirV version, atleast 1.3 is required"),
      string_static("Multiple SpirV entrypoints are not supported"),
      string_static("Unsupported SpirV shader resource"),
      string_static("SpirV shader resource set exceeds maximum"),
      string_static("SpirV shader resource binding exceeds maximum"),
  };
  ASSERT(array_elems(msgs) == SpvError_Count, "Incorrect number of spv-error messages");
  return msgs[res];
}

static SpvData spv_consume(const SpvData data, const u32 amount) {
  diag_assert(data.size >= amount);
  return (SpvData){
      .ptr  = data.ptr + amount,
      .size = data.size - amount,
  };
}

static SpvData spv_read_version(const SpvData data, SpvVersion* out) {
  *out = (SpvVersion){.major = (u8)(*data.ptr >> 16), .minor = (u8)(*data.ptr >> 8)};
  return spv_consume(data, 1);
}

static SpvData spv_read_instruction_header(const SpvData data, SpvInstructionHeader* out) {
  *out = (SpvInstructionHeader){.opCode = (u16)*data.ptr, .opSize = (u16)(*data.ptr >> 16)};
  return spv_consume(data, 1);
}

static SpvData spv_read_string(const SpvData data, const u32 maxWordSize, String* out) {
  const u8* const strBegin = (u8*)data.ptr;
  const u8* const strMax   = strBegin + maxWordSize * sizeof(u32);
  const u8*       strItr   = strBegin;
  for (; strItr != strMax && *strItr != '\0'; ++strItr)
    ;
  *out = mem_from_to(strBegin, strItr);
  return spv_consume(data, (out->size + 1 + 3) / sizeof(u32)); // +1 null-term, +3 round up.
}

static SpvData spv_read_program(SpvData data, const u32 maxId, SpvProgram* out, SpvError* err) {
  *out = (SpvProgram){
      .ids     = alloc_alloc_array_t(g_alloc_scratch, SpvId, maxId),
      .idCount = maxId,
  };
  mem_set(mem_from_to(out->ids, out->ids + out->idCount), 0);

  while (data.size) {
    SpvInstructionHeader header;
    spv_read_instruction_header(data, &header);
    if (!header.opCode) {
      *err = SpvError_Malformed;
      return data;
    }
    switch (header.opCode) {
    case SpvOp_EntryPoint: {
      /**
       * Entry point definiton, we gather the execution model (stage) and the entryPointName here.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpEntryPoint
       */
      if (!string_is_empty(out->entryPointName)) {
        *err = SpvError_UnsupportedMultipleEntryPoints;
        return data;
      }
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      out->execModel = (SpvExecutionModel)data.ptr[1];
      spv_read_string(spv_consume(data, 3), header.opSize - 3, &out->entryPointName);
    } break;
    case SpvOp_Decorate: {
      /**
       * Id decoration, we can gather which descriptor set and binding an id belongs to.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpDecorate
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#Decoration
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 targetId = data.ptr[1];
      if (targetId >= maxId) {
        *err = SpvError_MalformedIdOutOfBounds;
        return data;
      }
      switch (data.ptr[2]) {
      case SpvDecoration_DescriptorSet:
        out->ids[targetId].set = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasSet;
        break;
      case SpvDecoration_Binding:
        out->ids[targetId].binding = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasBinding;
        break;
      }
    } break;
    case SpvOp_Variable: {
      /**
       * Variable declaration, gather the type and the storage class of the variable.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpVariable
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 typeId = data.ptr[1];
      const u32 id     = data.ptr[2];
      if (typeId >= maxId || id >= maxId) {
        *err = SpvError_MalformedIdOutOfBounds;
        return data;
      }
      if (out->ids[id].kind != SpvIdKind_Unknown) {
        *err = SpvError_MalformedDuplicateId;
        return data;
      }
      out->ids[id].kind         = SpvIdKind_Variable;
      out->ids[id].typeId       = typeId;
      out->ids[id].storageClass = (SpvStorageClass)data.ptr[3];
    } break;
    case SpvOp_TypePointer: {
      /**
       * Pointer type declaration.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpTypePointer
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id     = data.ptr[1];
      const u32 typeId = data.ptr[3];
      if (typeId >= maxId || id >= maxId) {
        *err = SpvError_MalformedIdOutOfBounds;
        return data;
      }
      if (out->ids[id].kind != SpvIdKind_Unknown) {
        *err = SpvError_MalformedDuplicateId;
        return data;
      }
      out->ids[id].kind         = SpvIdKind_TypePointer;
      out->ids[id].typeId       = typeId;
      out->ids[id].storageClass = (SpvStorageClass)data.ptr[2];
    } break;
    case SpvOp_TypeStruct: {
      /**
       * Struct declaration.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpTypeStruct
       */
      if (data.size < 2) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id = data.ptr[1];
      if (id >= maxId) {
        *err = SpvError_MalformedIdOutOfBounds;
        return data;
      }
      if (out->ids[id].kind != SpvIdKind_Unknown) {
        *err = SpvError_MalformedDuplicateId;
        return data;
      }
      out->ids[id].kind = SpvIdKind_TypeStruct;
    } break;
    case SpvOp_TypeSampledImage: {
      /**
       * Sampled image declaration.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpTypeSampledImage
       */
      if (data.size < 3) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id = data.ptr[1];
      if (id >= maxId) {
        *err = SpvError_MalformedIdOutOfBounds;
        return data;
      }
      if (out->ids[id].kind != SpvIdKind_Unknown) {
        *err = SpvError_MalformedDuplicateId;
        return data;
      }
      out->ids[id].kind = SpvIdKind_TypeSampledImage;
    } break;
    }
    data = spv_consume(data, header.opSize);
  }
  *err = SpvError_None;
  return data;
}

static AssetShaderKind spv_shader_kind(const SpvExecutionModel execModel) {
  switch (execModel) {
  case SpvExecutionModel_Vertex:
    return AssetShaderKind_SpvVertex;
  case SpvExecutionModel_Fragment:
  default:
    return AssetShaderKind_SpvFragment;
  }
}

static bool spv_is_resource(const SpvId* id) {
  if (id->kind != SpvIdKind_Variable) {
    return false;
  }
  switch (id->storageClass) {
  case SpvStorageClass_Uniform:
  case SpvStorageClass_UniformConstant:
  case SpvStorageClass_StorageBuffer:
    return true;
  default:
    return false;
  }
}

static AssetShaderResKind spv_resource_kind(
    const SpvProgram*     program,
    const u32             typeId,
    const SpvStorageClass varStorageClass,
    SpvError*             err) {
  diag_assert(typeId < program->idCount);

  const SpvId* id = &program->ids[typeId];
  switch (id->kind) {
  case SpvIdKind_TypePointer:
    return spv_resource_kind(program, id->typeId, varStorageClass, err);
  case SpvIdKind_TypeSampledImage:
    *err = SpvError_None;
    return AssetShaderResKind_Texture;
  case SpvIdKind_TypeStruct:
    switch (varStorageClass) {
    case SpvStorageClass_Uniform:
    case SpvStorageClass_UniformConstant:
      *err = SpvError_None;
      return AssetShaderResKind_UniformBuffer;
    case SpvStorageClass_StorageBuffer:
      *err = SpvError_None;
      return AssetShaderResKind_StorageBuffer;
    default:
      break;
    }
  default:
    *err = SpvError_UnsupportedShaderResource;
    return 0;
  }
}

static void spv_asset_shader_create(
    SpvProgram* program, AssetSource* src, AssetShaderComp* out, SpvError* err) {

  *out = (AssetShaderComp){
      .kind           = spv_shader_kind(program->execModel),
      .entryPointName = program->entryPointName,
      .resources = alloc_alloc_array_t(g_alloc_heap, AssetShaderRes, asset_shader_max_resources),
      .data      = src->data,
  };

  ASSERT(sizeof(u32) == asset_shader_max_bindings / 8, "Unsupported max shader bindings");
  u32 usedSlots[asset_shader_max_bindings] = {0};

  for (u32 i = 0; i != program->idCount; ++i) {
    const SpvId* id = &program->ids[i];
    if (LIKELY(!spv_is_resource(id))) {
      continue;
    }
    const AssetShaderResKind kind = spv_resource_kind(program, id->typeId, id->storageClass, err);
    if (UNLIKELY(*err)) {
      return;
    }
    if (!(id->flags & SpvIdFlags_HasSet) || !(id->flags & SpvIdFlags_HasBinding)) {
      *err = SpvError_MalformedResourceWithoutSetAndId;
      return;
    }
    if (id->set >= asset_shader_max_sets) {
      *err = SpvError_UnsupportedSetExceedsMax;
      return;
    }
    if (id->binding >= asset_shader_max_bindings) {
      *err = SpvError_UnsupportedBindingExceedsMax;
      return;
    }
    if (usedSlots[id->set] & (1 << id->binding)) {
      *err = SpvError_MalformedDuplicateBinding;
      return;
    }
    usedSlots[id->set] |= 1U << id->binding;
    out->resources[out->resourceCount++] = (AssetShaderRes){
        .kind    = kind,
        .set     = id->set,
        .binding = id->binding,
    };
  }
  *err = SpvError_None;
}

NORETURN static void spv_report_error(const SpvError err) {
  diag_crash_msg("Failed to parse SpirV shader, error: {}", fmt_text(spv_error_str(err)));
}

void asset_load_spv(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  /**
   * SpirV consists of 32 bit words so we interpret the file as a set of 32 bit words.
   * TODO: Convert to big-endian in case we're running on a big-endian system.
   */
  if (!bits_aligned(src->data.size, sizeof(u32))) {
    spv_report_error(SpvError_Malformed);
  }
  SpvData data = {.ptr = src->data.ptr, .size = src->data.size / sizeof(u32)};
  if (data.size < 5) {
    spv_report_error(SpvError_Malformed);
  }

  // Read the header.
  if (*data.ptr != spv_magic) {
    spv_report_error(SpvError_Malformed);
  }
  data = spv_consume(data, 1); // Spv magic number.
  SpvVersion version;
  data = spv_read_version(data, &version);
  if (version.major <= 1 && version.minor < 3) {
    spv_report_error(SpvError_UnsupportedVersion);
  }
  data            = spv_consume(data, 1); // Generators magic number.
  const u32 maxId = *data.ptr;
  data            = spv_consume(data, 2); // maxId + reserved.

  // Read the program.
  SpvError   err;
  SpvProgram program;
  data = spv_read_program(data, maxId, &program, &err);
  if (err) {
    spv_report_error(err);
  }

  // Create the asset.
  AssetShaderComp* asset = ecs_world_add_t(world, assetEntity, AssetShaderComp);
  spv_asset_shader_create(&program, src, asset, &err);
  if (err) {
    spv_report_error(err);
  }
  ecs_world_add_t(world, assetEntity, AssetShaderSourceComp, .src = src);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
}
