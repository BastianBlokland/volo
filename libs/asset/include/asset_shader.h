#pragma once
#include "ecs_module.h"

#define asset_shader_max_sets 5
#define asset_shader_max_bindings 8
#define asset_shader_max_resources (asset_shader_max_sets * asset_shader_max_bindings)

typedef enum {
  AssetShaderKind_SpvVertex,
  AssetShaderKind_SpvFragment,
} AssetShaderKind;

typedef enum {
  AssetShaderResKind_Texture,
  AssetShaderResKind_UniformBuffer,
  AssetShaderResKind_StorageBuffer,
} AssetShaderResKind;

typedef struct {
  AssetShaderResKind kind;
  u32                set, binding;
} AssetShaderRes;

ecs_comp_extern_public(AssetShaderComp) {
  AssetShaderKind kind;
  String          entryPointName;
  AssetShaderRes* resources;
  u32             resourceCount;
  String          data;
};
