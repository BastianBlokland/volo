#include "core_alloc.h"
#include "core_diag.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_shader_internal.h"

ecs_comp_define_public(AssetShaderComp);
ecs_comp_define_public(AssetShaderSourceComp);

DataMeta g_assetShaderMeta;

static void ecs_destruct_shader_comp(void* data) {
  AssetShaderComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetShaderMeta, mem_create(comp, sizeof(AssetShaderComp)));
}

static void ecs_destruct_shader_source_comp(void* data) {
  AssetShaderSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetShaderComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any shader-asset components for unloaded assets.
 */
ecs_system_define(UnloadShaderAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetShaderComp);
    ecs_utils_maybe_remove_t(world, entity, AssetShaderSourceComp);
  }
}

ecs_module_init(asset_shader_module) {
  ecs_register_comp(AssetShaderComp, .destructor = ecs_destruct_shader_comp);
  ecs_register_comp(AssetShaderSourceComp, .destructor = ecs_destruct_shader_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadShaderAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_shader(void) {
  // clang-format off
  data_reg_enum_t(g_dataReg, AssetShaderKind);
  data_reg_const_t(g_dataReg, AssetShaderKind, SpvVertex);
  data_reg_const_t(g_dataReg, AssetShaderKind, SpvFragment);

  data_reg_enum_multi_t(g_dataReg, AssetShaderFlags);
  data_reg_const_t(g_dataReg, AssetShaderFlags, MayKill);

  data_reg_enum_t(g_dataReg, AssetShaderResKind);
  data_reg_const_t(g_dataReg, AssetShaderResKind, Texture2D);
  data_reg_const_t(g_dataReg, AssetShaderResKind, TextureCube);
  data_reg_const_t(g_dataReg, AssetShaderResKind, UniformBuffer);
  data_reg_const_t(g_dataReg, AssetShaderResKind, StorageBuffer);

  data_reg_struct_t(g_dataReg, AssetShaderRes);
  data_reg_field_t(g_dataReg, AssetShaderRes, kind, t_AssetShaderResKind);
  data_reg_field_t(g_dataReg, AssetShaderRes, set, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetShaderRes, binding, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetShaderSpec);
  data_reg_field_t(g_dataReg, AssetShaderSpec, type, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetShaderSpec, defVal, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetShaderSpec, binding, data_prim_t(u8));

  data_reg_struct_t(g_dataReg, AssetShaderComp);
  data_reg_field_t(g_dataReg, AssetShaderComp, kind, t_AssetShaderKind);
  data_reg_field_t(g_dataReg, AssetShaderComp, flags, t_AssetShaderFlags, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetShaderComp, killSpecConstMask, data_prim_t(u16));
  data_reg_field_t(g_dataReg, AssetShaderComp, inputs, data_prim_t(u8), .container = DataContainer_InlineArray, .fixedCount = asset_shader_max_inputs);
  data_reg_field_t(g_dataReg, AssetShaderComp, outputs, data_prim_t(u8), .container = DataContainer_InlineArray, .fixedCount = asset_shader_max_outputs);
  data_reg_field_t(g_dataReg, AssetShaderComp, entryPoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetShaderComp, resources, t_AssetShaderRes, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetShaderComp, specs, t_AssetShaderSpec, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetShaderComp, data, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetShaderMeta = data_meta_t(t_AssetShaderComp);
}

void asset_load_shader_bin(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {

  AssetShaderComp shader;
  DataReadResult  result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetShaderMeta, mem_var(shader), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary shader",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetShaderComp) = shader;
  ecs_world_add_t(world, entity, AssetShaderSourceComp, .src = src);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}

u32 asset_shader_type_size(const AssetShaderType type) {
  diag_assert(type < AssetShaderType_Count);
  static const u32 g_sizes[AssetShaderType_Count] = {
      [AssetShaderType_bool]  = sizeof(u32), // NOTE: (Vulkan) shader booleans take 4 bytes.
      [AssetShaderType_u8]    = sizeof(u8),
      [AssetShaderType_i8]    = sizeof(i8),
      [AssetShaderType_u16]   = sizeof(u16),
      [AssetShaderType_i16]   = sizeof(i16),
      [AssetShaderType_u32]   = sizeof(u32),
      [AssetShaderType_i32]   = sizeof(i32),
      [AssetShaderType_u64]   = sizeof(u64),
      [AssetShaderType_i64]   = sizeof(i64),
      [AssetShaderType_f16]   = sizeof(f16),
      [AssetShaderType_f32]   = sizeof(f32),
      [AssetShaderType_f32v2] = sizeof(f32) * 2,
      [AssetShaderType_f32v3] = sizeof(f32) * 3,
      [AssetShaderType_f32v4] = sizeof(f32) * 4,
      [AssetShaderType_f64]   = sizeof(f64),
  };
  return g_sizes[type];
}

String asset_shader_type_name(const AssetShaderType type) {
  // clang-format off
  switch (type) {
  case AssetShaderType_bool:    return string_lit("bool");
  case AssetShaderType_u8:      return string_lit("u8");
  case AssetShaderType_i8:      return string_lit("i8");
  case AssetShaderType_u16:     return string_lit("u16");
  case AssetShaderType_i16:     return string_lit("i16");
  case AssetShaderType_u32:     return string_lit("u32");
  case AssetShaderType_i32:     return string_lit("i32");
  case AssetShaderType_u64:     return string_lit("u64");
  case AssetShaderType_i64:     return string_lit("i64");
  case AssetShaderType_f16:     return string_lit("f16");
  case AssetShaderType_f32:     return string_lit("f32");
  case AssetShaderType_f32v2:   return string_lit("f32v2");
  case AssetShaderType_f32v3:   return string_lit("f32v3");
  case AssetShaderType_f32v4:   return string_lit("f32v4");
  case AssetShaderType_f64:     return string_lit("f64");
  case AssetShaderType_None:    return string_lit("none");
  case AssetShaderType_Unknown: return string_lit("unknown");
  case AssetShaderType_Count:   break;
  }
  // clang-format on
  diag_crash_msg("Invalid shader type");
}
