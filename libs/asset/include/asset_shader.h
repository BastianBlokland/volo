#pragma once
#include "ecs_module.h"

#define asset_shader_max_sets 5
#define asset_shader_max_bindings 8
#define asset_shader_max_resources (asset_shader_max_sets * asset_shader_max_bindings)
#define asset_shader_max_specs 16
#define asset_shader_max_inputs 16
#define asset_shader_max_outputs 16

typedef enum {
  AssetShaderKind_SpvVertex,
  AssetShaderKind_SpvFragment,

  AssetShaderKind_Count,
} AssetShaderKind;

typedef enum {
  AssetShaderFlags_MayKill = 1 << 0, // Shader might kill (aka 'discard') the invocation.
} AssetShaderFlags;

typedef enum {
  AssetShaderResKind_Texture2D,
  AssetShaderResKind_TextureCube,
  AssetShaderResKind_UniformBuffer,
  AssetShaderResKind_StorageBuffer,

  AssetShaderResKind_Count,
} AssetShaderResKind;

typedef struct {
  AssetShaderResKind kind;
  u32                set, binding;
} AssetShaderRes;

typedef enum {
  AssetShaderType_bool,
  AssetShaderType_u8,
  AssetShaderType_i8,
  AssetShaderType_u16,
  AssetShaderType_i16,
  AssetShaderType_u32,
  AssetShaderType_i32,
  AssetShaderType_u64,
  AssetShaderType_i64,
  AssetShaderType_f16,
  AssetShaderType_f32,
  AssetShaderType_f64,

  AssetShaderType_Count,
} AssetShaderType;

typedef enum {
  AssetShaderSpecDef_False,
  AssetShaderSpecDef_True,
  AssetShaderSpecDef_Other, // Non boolean spec constant.
} AssetShaderSpecDef;

typedef struct {
  u8 type;   // AssetShaderType.
  u8 defVal; // AssetShaderSpecDef.
  u8 binding;
} AssetShaderSpec;

ecs_comp_extern_public(AssetShaderComp) {
  AssetShaderKind  kind;
  AssetShaderFlags flags;
  u16              killSpecConstMask; // Mask of spec constants that need to be true for kill inst.
  u16              inputMask, outputMask;
  String           entryPoint;
  struct {
    AssetShaderRes* values;
    usize           count;
  } resources;
  struct {
    AssetShaderSpec* values;
    usize            count;
  } specs;
  String data;
};
