#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_shader_internal.h"

/**
 * Spir-V (Standard Portable Intermediate Representation 5)
 * Spec: https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html
 */

#define spv_magic 0x07230203

typedef enum {
  SpvOp_EntryPoint        = 15,
  SpvOp_TypeBool          = 20,
  SpvOp_TypeInt           = 21,
  SpvOp_TypeFloat         = 22,
  SpvOp_TypeSampledImage  = 27,
  SpvOp_TypeStruct        = 30,
  SpvOp_TypePointer       = 32,
  SpvOp_SpecConstantTrue  = 48,
  SpvOp_SpecConstantFalse = 49,
  SpvOp_SpecConstant      = 50,
  SpvOp_Variable          = 59,
  SpvOp_Decorate          = 71,
} SpvOp;

typedef enum {
  SpvDecoration_SpecId        = 1,
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
  SpvIdKind_SpecConstant,
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
  String            entryPoint;
  SpvId*            ids;
  u32               idCount;
  u32               wellknownTypes[AssetShaderType_Count];
} SpvProgram;

typedef enum {
  SpvError_None = 0,
  SpvError_Malformed,
  SpvError_MalformedIdOutOfBounds,
  SpvError_MalformedDuplicateId,
  SpvError_MalformedResourceWithoutSetAndId,
  SpvError_MalformedDuplicateBinding,
  SpvError_MalformedSpecWithoutBinding,
  SpvError_UnsupportedVersion,
  SpvError_UnsupportedMultipleEntryPoints,
  SpvError_UnsupportedShaderResource,
  SpvError_UnsupportedSpecConstantType,
  SpvError_UnsupportedSetExceedsMax,
  SpvError_UnsupportedBindingExceedsMax,

  SpvError_Count,
} SpvError;

static String spv_error_str(SpvError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Malformed SpirV data"),
      string_static("SpirV id out of bounds"),
      string_static("Duplicate SpirV id"),
      string_static("SpirV shader resource without set and binding"),
      string_static("SpirV shader resource binding already used in this set"),
      string_static("SpirV shader specialization constant without a binding"),
      string_static("Unsupported SpirV version, atleast 1.3 is required"),
      string_static("Multiple SpirV entrypoints are not supported"),
      string_static("Unsupported SpirV shader resource"),
      string_static("Unsupported SpirV specialization constant type"),
      string_static("SpirV shader resource set exceeds maximum"),
      string_static("SpirV shader resource binding exceeds maximum"),
  };
  ASSERT(array_elems(g_msgs) == SpvError_Count, "Incorrect number of spv-error messages");
  return g_msgs[res];
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
  return spv_consume(data, ((u32)out->size + 1 + 3) / sizeof(u32)); // +1 null-term, +3 round up.
}

static bool spv_validate_id(const u32 id, const SpvProgram* prog, SpvError* err) {
  if (id >= prog->idCount) {
    *err = SpvError_MalformedIdOutOfBounds;
    return false;
  }
  return true;
}

static bool spv_validate_new_id(const u32 id, const SpvProgram* prog, SpvError* err) {
  if (!spv_validate_id(id, prog, err)) {
    return false;
  }
  if (prog->ids[id].kind != SpvIdKind_Unknown) {
    *err = SpvError_MalformedDuplicateId;
    return false;
  }
  return true;
}

static SpvData spv_read_program(SpvData data, const u32 maxId, SpvProgram* out, SpvError* err) {
  *out = (SpvProgram){
      .ids     = alloc_array_t(g_alloc_scratch, SpvId, maxId),
      .idCount = maxId,
  };
  mem_set(mem_from_to(out->ids, out->ids + out->idCount), 0);
  array_for_t(out->wellknownTypes, u32, itr) { *itr = sentinel_u32; }

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
      if (!string_is_empty(out->entryPoint)) {
        *err = SpvError_UnsupportedMultipleEntryPoints;
        return data;
      }
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      out->execModel = (SpvExecutionModel)data.ptr[1];
      spv_read_string(spv_consume(data, 3), header.opSize - 3, &out->entryPoint);
    } break;
    case SpvOp_TypeBool: {
      /**
       * Bool type declaration.
       * https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#OpTypeBool
       */
      if (data.size < 2) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 typeId = data.ptr[1];
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      out->wellknownTypes[AssetShaderType_bool] = typeId;
    } break;
    case SpvOp_TypeInt: {
      /**
       * Int type declaration.
       * https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#OpTypeInt
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 typeId = data.ptr[1];
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      const u32 width      = data.ptr[2];
      const u32 signedness = data.ptr[3];
      switch (width) {
      case 8:
        out->wellknownTypes[signedness ? AssetShaderType_i8 : AssetShaderType_u8] = typeId;
        break;
      case 16:
        out->wellknownTypes[signedness ? AssetShaderType_i16 : AssetShaderType_u16] = typeId;
        break;
      case 32:
        out->wellknownTypes[signedness ? AssetShaderType_i32 : AssetShaderType_u32] = typeId;
        break;
      case 64:
        out->wellknownTypes[signedness ? AssetShaderType_i64 : AssetShaderType_u64] = typeId;
        break;
      }
    } break;
    case SpvOp_TypeFloat: {
      /**
       * Float type declaration.
       * https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#OpTypeFloat
       */
      if (data.size < 3) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 typeId = data.ptr[1];
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      const u32 width = data.ptr[2];
      switch (width) {
      case 16:
        out->wellknownTypes[AssetShaderType_f16] = typeId;
        break;
      case 32:
        out->wellknownTypes[AssetShaderType_f32] = typeId;
        break;
      case 64:
        out->wellknownTypes[AssetShaderType_f64] = typeId;
        break;
      }
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
      if (!spv_validate_id(targetId, out, err)) {
        return data;
      }
      switch (data.ptr[2]) {
      case SpvDecoration_SpecId:
        out->ids[targetId].binding = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasBinding;
        break;
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
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      if (!spv_validate_new_id(id, out, err)) {
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
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      if (!spv_validate_new_id(id, out, err)) {
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
      if (!spv_validate_new_id(id, out, err)) {
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
      if (!spv_validate_new_id(id, out, err)) {
        return data;
      }
      out->ids[id].kind = SpvIdKind_TypeSampledImage;
    } break;
    case SpvOp_SpecConstant:
    case SpvOp_SpecConstantTrue:
    case SpvOp_SpecConstantFalse: {
      /**
       * Specialization constant declaration.
       * https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#OpSpecConstantOp
       */
      if (data.size < 3) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 typeId = data.ptr[1];
      const u32 id     = data.ptr[2];
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      if (!spv_validate_new_id(id, out, err)) {
        return data;
      }
      out->ids[id].kind   = SpvIdKind_SpecConstant;
      out->ids[id].typeId = typeId;
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

static AssetShaderType spv_lookup_type(SpvProgram* program, const u32 typeId, SpvError* err) {
  for (AssetShaderType type = 0; type != AssetShaderType_Count; ++type) {
    if (program->wellknownTypes[type] == typeId) {
      return type;
    }
  }
  *err = SpvError_UnsupportedSpecConstantType;
  return 0;
}

static void spv_asset_shader_create(
    SpvProgram* program, AssetSource* src, AssetShaderComp* out, SpvError* err) {

  *out = (AssetShaderComp){
      .kind             = spv_shader_kind(program->execModel),
      .entryPoint       = program->entryPoint,
      .resources.values = alloc_array_t(g_alloc_heap, AssetShaderRes, asset_shader_max_resources),
      .specs.values     = alloc_array_t(g_alloc_heap, AssetShaderSpec, asset_shader_max_specs),
      .data             = src->data,
  };

  ASSERT(sizeof(u32) >= asset_shader_max_bindings / 8, "Unsupported max shader bindings");
  u32 usedResSlots[asset_shader_max_bindings] = {0};
  u32 usedSpecSlots                           = 0;

  for (u32 i = 0; i != program->idCount; ++i) {
    const SpvId* id = &program->ids[i];
    if (spv_is_resource(id)) {
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
      if (usedResSlots[id->set] & (1 << id->binding)) {
        *err = SpvError_MalformedDuplicateBinding;
        return;
      }
      usedResSlots[id->set] |= 1 << id->binding;
      out->resources.values[out->resources.count++] = (AssetShaderRes){
          .kind    = kind,
          .set     = id->set,
          .binding = id->binding,
      };
    } else if (id->kind == SpvIdKind_SpecConstant) {
      if (!(id->flags & SpvIdFlags_HasBinding)) {
        *err = SpvError_MalformedSpecWithoutBinding;
        return;
      }
      if (id->binding >= asset_shader_max_specs) {
        *err = SpvError_UnsupportedBindingExceedsMax;
        return;
      }
      if (usedSpecSlots & (1 << id->binding)) {
        *err = SpvError_MalformedDuplicateBinding;
        return;
      }
      const AssetShaderType type = spv_lookup_type(program, id->typeId, err);
      if (UNLIKELY(*err)) {
        return;
      }
      usedSpecSlots |= 1 << id->binding;
      out->specs.values[out->specs.count++] = (AssetShaderSpec){
          .type    = type,
          .binding = id->binding,
      };
    }
  }
  *err = SpvError_None;
}

static void spv_load_fail(EcsWorld* world, const EcsEntityId entity, const SpvError err) {
  log_e("Failed to parse SpirV shader", log_param("error", fmt_text(spv_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_spv(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  /**
   * SpirV consists of 32 bit words so we interpret the file as a set of 32 bit words.
   * TODO: Convert to big-endian in case we're running on a big-endian system.
   */
  if (!bits_aligned(src->data.size, sizeof(u32))) {
    spv_load_fail(world, entity, SpvError_Malformed);
    goto Error;
  }
  SpvData data = {.ptr = src->data.ptr, .size = (u32)src->data.size / sizeof(u32)};
  if (data.size < 5) {
    spv_load_fail(world, entity, SpvError_Malformed);
    goto Error;
  }

  // Read the header.
  if (*data.ptr != spv_magic) {
    spv_load_fail(world, entity, SpvError_Malformed);
    goto Error;
  }
  data = spv_consume(data, 1); // Spv magic number.
  SpvVersion version;
  data = spv_read_version(data, &version);
  if (version.major <= 1 && version.minor < 3) {
    spv_load_fail(world, entity, SpvError_UnsupportedVersion);
    goto Error;
  }
  data            = spv_consume(data, 1); // Generators magic number.
  const u32 maxId = *data.ptr;
  data            = spv_consume(data, 2); // maxId + reserved.

  // Read the program.
  SpvError   err;
  SpvProgram program;
  data = spv_read_program(data, maxId, &program, &err);
  if (err) {
    spv_load_fail(world, entity, err);
    goto Error;
  }

  // Create the asset.
  AssetShaderComp* asset = ecs_world_add_t(world, entity, AssetShaderComp);
  spv_asset_shader_create(&program, src, asset, &err);
  if (err) {
    spv_load_fail(world, entity, err);
    // NOTE: 'AssetShaderComp' will be cleaned up by 'UnloadShaderAssetSys'.
    goto Error;
  }
  ecs_world_add_t(world, entity, AssetShaderSourceComp, .src = src);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
