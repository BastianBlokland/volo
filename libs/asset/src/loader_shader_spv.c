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
#define spv_spec_branches_max 5

typedef enum {
  SpvOp_EntryPoint        = 15,
  SpvOp_TypeBool          = 20,
  SpvOp_TypeInt           = 21,
  SpvOp_TypeFloat         = 22,
  SpvOp_TypeImage         = 25,
  SpvOp_TypeSampledImage  = 27,
  SpvOp_TypeStruct        = 30,
  SpvOp_TypePointer       = 32,
  SpvOp_SpecConstantTrue  = 48,
  SpvOp_SpecConstantFalse = 49,
  SpvOp_SpecConstant      = 50,
  SpvOp_Variable          = 59,
  SpvOp_Decorate          = 71,
  SpvOp_Label             = 248,
  SpvOp_Branch            = 249,
  SpvOp_BranchConditional = 250,
  SpvOp_Switch            = 251,
  SpvOp_Kill              = 252,
} SpvOp;

typedef enum {
  SpvDecoration_SpecId        = 1,
  SpvDecoration_Location      = 30,
  SpvDecoration_Binding       = 33,
  SpvDecoration_DescriptorSet = 34,
} SpvDecoration;

typedef enum {
  SpvStorageClass_UniformConstant = 0,
  SpvStorageClass_Input           = 1,
  SpvStorageClass_Uniform         = 2,
  SpvStorageClass_Output          = 3,
  SpvStorageClass_StorageBuffer   = 12,
} SpvStorageClass;

typedef enum {
  SpvExecutionModel_Vertex   = 0,
  SpvExecutionModel_Fragment = 4,
} SpvExecutionModel;

typedef enum {
  SpvImageDim_2D   = 1,
  SpvImageDim_Cube = 3,
} SpvImageDim;

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

typedef u32 SpvInstructionId;

typedef enum {
  SpvIdKind_Unknown,
  SpvIdKind_Variable,
  SpvIdKind_TypePointer,
  SpvIdKind_TypeStruct,
  SpvIdKind_TypeImage2D,
  SpvIdKind_TypeImageCube,
  SpvIdKind_TypeSampledImage,
  SpvIdKind_SpecConstant,
  SpvIdKind_Label,
} SpvIdKind;

typedef enum {
  SpvIdFlags_HasSet           = 1 << 0,
  SpvIdFlags_HasBinding       = 1 << 1,
  SpvIdFlags_SpecDefaultTrue  = 1 << 2,
  SpvIdFlags_SpecDefaultFalse = 1 << 3,
} SpvIdFlags;

typedef struct {
  SpvIdKind        kind : 8;
  SpvIdFlags       flags : 8;
  SpvStorageClass  storageClass : 8;
  u32              set, binding, typeId;
  SpvInstructionId declInstruction; // Identifier of the instruction that declared this id.
} SpvId;

typedef enum {
  SpvFlags_HasBackwardBranches = 1 << 0, // eg Loops.
} SpvFlags;

/**
 * Conditional branch on a specialization constant.
 * Useful to determine if code is reachable given specific specialization constants.
 */
typedef struct {
  u32 specBinding;
  u32 labelTrue, labelFalse;
} SpvSpecBranch;

typedef struct {
  SpvFlags          flags : 8;
  SpvExecutionModel execModel : 8;
  String            entryPoint;
  SpvId*            ids;
  u32               idCount;
  SpvInstructionId  killInstruction;
  u32               wellknownTypes[AssetShaderType_Count];
  SpvSpecBranch     specBranches[spv_spec_branches_max];
  u32               specBranchCount;
} SpvProgram;

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
      .ids             = alloc_array_t(g_allocScratch, SpvId, maxId),
      .idCount         = maxId,
      .killInstruction = sentinel_u32,
  };
  mem_set(mem_from_to(out->ids, out->ids + out->idCount), 0);
  array_for_t(out->wellknownTypes, u32, itr) { *itr = sentinel_u32; }

  SpvInstructionId instructionId = 0;
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
      case SpvDecoration_Location:
        out->ids[targetId].binding = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasBinding;
        break;
      case SpvDecoration_Binding:
        out->ids[targetId].binding = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasBinding;
        break;
      case SpvDecoration_DescriptorSet:
        out->ids[targetId].set = data.ptr[3];
        out->ids[targetId].flags |= SpvIdFlags_HasSet;
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
      out->ids[id].kind            = SpvIdKind_Variable;
      out->ids[id].typeId          = typeId;
      out->ids[id].storageClass    = (SpvStorageClass)data.ptr[3];
      out->ids[id].declInstruction = instructionId;
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
      out->ids[id].kind            = SpvIdKind_TypePointer;
      out->ids[id].typeId          = typeId;
      out->ids[id].storageClass    = (SpvStorageClass)data.ptr[2];
      out->ids[id].declInstruction = instructionId;
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
      out->ids[id].kind            = SpvIdKind_TypeStruct;
      out->ids[id].declInstruction = instructionId;
    } break;
    case SpvOp_TypeImage: {
      /**
       * Image declaration.
       * https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#OpTypeImage
       */
      if (data.size < 5) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id = data.ptr[1];
      if (!spv_validate_new_id(id, out, err)) {
        return data;
      }
      switch (data.ptr[3]) {
      case SpvImageDim_2D:
        out->ids[id].kind = SpvIdKind_TypeImage2D;
        break;
      case SpvImageDim_Cube:
        out->ids[id].kind = SpvIdKind_TypeImageCube;
        break;
      default:
        *err = SpvError_UnsupportedImageType;
        return data;
      }
      out->ids[id].declInstruction = instructionId;
    } break;
    case SpvOp_TypeSampledImage: {
      /**
       * Sampled image declaration.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpTypeSampledImage
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id     = data.ptr[1];
      const u32 typeId = data.ptr[2];
      if (!spv_validate_new_id(id, out, err)) {
        return data;
      }
      if (!spv_validate_id(typeId, out, err)) {
        return data;
      }
      out->ids[id].kind            = SpvIdKind_TypeSampledImage;
      out->ids[id].typeId          = typeId;
      out->ids[id].declInstruction = instructionId;
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
      out->ids[id].kind            = SpvIdKind_SpecConstant;
      out->ids[id].typeId          = typeId;
      out->ids[id].declInstruction = instructionId;
      // Track default values for boolean spec constants.
      if (header.opCode == SpvOp_SpecConstantTrue) {
        out->ids[id].flags |= SpvIdFlags_SpecDefaultTrue;
      } else if (header.opCode == SpvOp_SpecConstantFalse) {
        out->ids[id].flags |= SpvIdFlags_SpecDefaultFalse;
      }
    } break;
    case SpvOp_Label: {
      /**
       * label declaration.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpLabel
       */
      if (data.size < 2) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 id = data.ptr[1];
      if (!spv_validate_new_id(id, out, err)) {
        return data;
      }
      out->ids[id].kind            = SpvIdKind_Label;
      out->ids[id].declInstruction = instructionId;
    } break;
    case SpvOp_Branch: {
      /**
       * Branch instruction.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpBranch
       */
      if (data.size < 2) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 labelId = data.ptr[1];
      if (!spv_validate_id(labelId, out, err)) {
        return data;
      }
      if (out->ids[labelId].kind != SpvIdKind_Unknown) {
        out->flags |= SpvFlags_HasBackwardBranches; // Seen this label before: backward branch.
      }
    } break;
    case SpvOp_BranchConditional: {
      /**
       * Branch-conditional instruction.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpBranchConditional
       */
      if (data.size < 4) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 conditionId  = data.ptr[1];
      const u32 labelIdTrue  = data.ptr[2];
      const u32 labelIdFalse = data.ptr[3];
      if (!spv_validate_id(conditionId, out, err)) {
        return data;
      }
      if (!spv_validate_id(labelIdTrue, out, err)) {
        return data;
      }
      if (!spv_validate_id(labelIdFalse, out, err)) {
        return data;
      }
      if (out->ids[labelIdTrue].kind != SpvIdKind_Unknown) {
        out->flags |= SpvFlags_HasBackwardBranches; // Seen this label before: backward branch.
      }
      if (out->ids[labelIdFalse].kind != SpvIdKind_Unknown) {
        out->flags |= SpvFlags_HasBackwardBranches; // Seen this label before: backward branch.
      }
      // Track specialization constant branches.
      if (out->ids[conditionId].kind == SpvIdKind_SpecConstant) {
        if (out->specBranchCount == spv_spec_branches_max) {
          *err = SpvError_Malformed;
          return data;
        }
        out->specBranches[out->specBranchCount++] = (SpvSpecBranch){
            .specBinding = out->ids[conditionId].binding,
            .labelTrue   = labelIdTrue,
            .labelFalse  = labelIdFalse,
        };
      }
    } break;
    case SpvOp_Switch: {
      /**
       * Switch instruction.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpSwitch
       */
      if (header.opSize < 3 || data.size < header.opSize) {
        *err = SpvError_Malformed;
        return data;
      }
      const u32 labelIdDefault = data.ptr[2];
      if (!spv_validate_id(labelIdDefault, out, err)) {
        return data;
      }
      if (out->ids[labelIdDefault].kind != SpvIdKind_Unknown) {
        out->flags |= SpvFlags_HasBackwardBranches; // Seen this label before: backward branch.
      }
      const u32 targetCount = (header.opSize - 3) / 2;
      for (u32 targetIdx = 0; targetIdx != targetCount; ++targetIdx) {
        const u32 labelIdTarget = data.ptr[3 + (targetIdx * 2) + 1];
        if (out->ids[labelIdTarget].kind != SpvIdKind_Unknown) {
          out->flags |= SpvFlags_HasBackwardBranches; // Seen this label before: backward branch.
        }
      }
    } break;
    case SpvOp_Kill:
      /**
       * Kill instruction.
       * https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#OpKill
       */
      if (sentinel_check(out->killInstruction)) {
        out->killInstruction = instructionId;
      } else {
        *err = SpvError_MultipleKillInstructions;
        return data;
      }
      break;
    }
    ++instructionId;
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

static bool spv_is_specialization(const SpvId* id) { return id->kind == SpvIdKind_SpecConstant; }

static bool spv_is_input(const SpvId* id) {
  if (id->kind != SpvIdKind_Variable) {
    return false;
  }
  return id->storageClass == SpvStorageClass_Input && (id->flags & SpvIdFlags_HasBinding) != 0;
}

static bool spv_is_output(const SpvId* id) {
  if (id->kind != SpvIdKind_Variable) {
    return false;
  }
  return id->storageClass == SpvStorageClass_Output && (id->flags & SpvIdFlags_HasBinding) != 0;
}

static AssetShaderSpecDef spv_specialization_default(const SpvId* id) {
  diag_assert(id->kind == SpvIdKind_SpecConstant);
  if (id->flags & SpvIdFlags_SpecDefaultTrue) {
    return AssetShaderSpecDef_True;
  }
  if (id->flags & SpvIdFlags_SpecDefaultFalse) {
    return AssetShaderSpecDef_False;
  }
  return AssetShaderSpecDef_Other;
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
  case SpvIdKind_TypeImage2D:
    *err = SpvError_None;
    return AssetShaderResKind_Texture2D;
  case SpvIdKind_TypeImageCube:
    *err = SpvError_None;
    return AssetShaderResKind_TextureCube;
  case SpvIdKind_TypeSampledImage:
    return spv_resource_kind(program, id->typeId, varStorageClass, err);
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

static SpvInstructionId spv_label_instruction(SpvProgram* program, const u32 labelId) {
  diag_assert(labelId < program->idCount);
  if (program->ids[labelId].kind != SpvIdKind_Label) {
    return sentinel_u32;
  }
  return program->ids[labelId].declInstruction;
}

/**
 * Compute a mask of specialization constants that need to be true to reach the given instruction.
 *
 * NOTE: This is conservative check, spec constants will only be added if we know for sure that
 * control flow cannot reach the instruction without it being true.
 */
static u16 spv_instruction_spec_mask(SpvProgram* program, const SpvInstructionId instruction) {
  if (program->flags & SpvFlags_HasBackwardBranches) {
    /**
     * Creating specialization-constant masks for shaders with backwards branches (eg loops)
     * requires tracking more of the control flow.
     */
    return 0;
  }
  /**
   * Construct a mask of all the specialization-constants that need to be 'true' to be able to reach
   * this instruction.
   */
  u16 mask = 0;
  for (u32 i = 0; i != program->specBranchCount; ++i) {
    const SpvSpecBranch*   specBranch = &program->specBranches[i];
    const SpvInstructionId instTrue   = spv_label_instruction(program, specBranch->labelTrue);
    const SpvInstructionId instFalse  = spv_label_instruction(program, specBranch->labelFalse);
    if (UNLIKELY(sentinel_check(instTrue) || sentinel_check(instFalse))) {
      continue; // TODO: Getting here means the spir-v is invalid; report as an error.
    }
    if (instruction > instTrue && instruction < instFalse) {
      // Instruction will only be reached if the specialization constant is true.
      mask |= 1 << specBranch->specBinding;
    }
  }
  return mask;
}

static void spv_asset_shader_create(
    SpvProgram* program, const DataMem input, AssetShaderComp* out, SpvError* err) {

  *out = (AssetShaderComp){
      .kind       = spv_shader_kind(program->execModel),
      .entryPoint = string_maybe_dup(g_allocHeap, program->entryPoint),
      .data       = input,
  };

  if (!sentinel_check(program->killInstruction)) {
    out->flags |= AssetShaderFlags_MayKill;
    out->killSpecConstMask = spv_instruction_spec_mask(program, program->killInstruction);
  }

  ASSERT(sizeof(u32) >= asset_shader_max_bindings / 8, "Unsupported max shader bindings");
  ASSERT(asset_shader_max_specs <= u8_max, "Spec bindings have to be addressable using 8 bit");

  AssetShaderRes resources[asset_shader_max_resources];
  u32            resourceCount = 0;

  AssetShaderSpec specs[asset_shader_max_bindings];
  usize           specCount = 0;

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
      resources[resourceCount++] = (AssetShaderRes){
          .kind    = kind,
          .set     = id->set,
          .binding = id->binding,
      };
    } else if (spv_is_specialization(id)) {
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
      specs[specCount++] = (AssetShaderSpec){
          .type    = (u8)type,
          .defVal  = (u8)spv_specialization_default(id),
          .binding = (u8)id->binding,
      };
    } else if (spv_is_input(id)) {
      if (id->binding >= asset_shader_max_inputs) {
        *err = SpvError_UnsupportedInputExceedsMax;
        return;
      }
      out->inputMask |= 1 << id->binding;
    } else if (spv_is_output(id)) {
      if (id->binding >= asset_shader_max_outputs) {
        *err = SpvError_UnsupportedOutputExceedsMax;
        return;
      }
      out->outputMask |= 1 << id->binding;
    }
  }

  if (resourceCount) {
    out->resources.values = alloc_array_t(g_allocHeap, AssetShaderRes, resourceCount);
    out->resources.count  = resourceCount;
    mem_cpy(
        mem_from_to(out->resources.values, out->resources.values + resourceCount),
        mem_from_to(resources, resources + resourceCount));
  }
  if (specCount) {
    out->specs.values = alloc_array_t(g_allocHeap, AssetShaderSpec, specCount);
    out->specs.count  = specCount;
    mem_cpy(
        mem_from_to(out->specs.values, out->specs.values + specCount),
        mem_from_to(specs, specs + specCount));
  }

  *err = SpvError_None;
}

String spv_err_str(const SpvError res) {
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
      string_static("SpirV shader input binding exceeds maximum"),
      string_static("SpirV shader output binding exceeds maximum"),
      string_static("SpirV shader uses an unsupported image type (only 2D and Cube are supported)"),
      string_static("SpirV shader uses multiple kill (aka discard) instructions"),
      string_static("SpirV shader uses too many branches on specialization constants"),
  };
  ASSERT(array_elems(g_msgs) == SpvError_Count, "Incorrect number of spv-error messages");
  return g_msgs[res];
}

SpvError spv_init(EcsWorld* world, const EcsEntityId entity, const DataMem input) {
  /**
   * SpirV consists of 32 bit words so we interpret the file as a set of 32 bit words.
   * TODO: Convert to big-endian in case we're running on a big-endian system.
   */
  if (UNLIKELY(!bits_aligned(input.size, sizeof(u32)))) {
    return SpvError_Malformed;
  }
  SpvData data = {.ptr = input.ptr, .size = (u32)input.size / sizeof(u32)};
  if (UNLIKELY(data.size < 5)) {
    return SpvError_Malformed;
  }

  // Read the header.
  if (UNLIKELY(*data.ptr != spv_magic)) {
    return SpvError_Malformed;
  }
  data = spv_consume(data, 1); // Spv magic number.
  SpvVersion version;
  data = spv_read_version(data, &version);
  if (UNLIKELY(version.major != 1 || version.minor != 3)) {
    return SpvError_UnsupportedVersion;
  }
  data            = spv_consume(data, 1); // Generators magic number.
  const u32 maxId = *data.ptr;
  data            = spv_consume(data, 2); // maxId + reserved.

  // Read the program.
  SpvError   err;
  SpvProgram program;
  data = spv_read_program(data, maxId, &program, &err);
  if (UNLIKELY(err)) {
    return err;
  }

  // Create the asset.
  AssetShaderComp* asset = ecs_world_add_t(world, entity, AssetShaderComp);
  spv_asset_shader_create(&program, input, asset, &err);
  if (UNLIKELY(err)) {
    // NOTE: 'AssetShaderComp' will be cleaned up by 'UnloadShaderAssetSys'.
    return err;
  }

  return SpvError_None;
}

void asset_load_shader_spv(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {

  const SpvError err = spv_init(world, entity, data_mem_create_ext(src->data));
  if (err) {
    log_e(
        "Failed to load SpirV shader",
        log_param("id", fmt_text(id)),
        log_param("error", fmt_text(spv_err_str(err))));

    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
  } else {
    ecs_world_add_t(world, entity, AssetShaderSourceComp, .src = src);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  }
}
