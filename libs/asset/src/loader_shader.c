#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "loader_shader_internal.h"

ecs_comp_define_public(AssetShaderComp);
ecs_comp_define_public(AssetShaderSourceComp);

DataMeta g_assetShaderDataDef;

static void ecs_destruct_shader_comp(void* data) {
  AssetShaderComp* comp = data;
  if (comp->resources.values) {
    alloc_free_array_t(g_allocHeap, comp->resources.values, comp->resources.count);
  }
  if (comp->specs.values) {
    alloc_free_array_t(g_allocHeap, comp->specs.values, comp->specs.count);
  }
}

static void ecs_destruct_shader_source_comp(void* data) {
  AssetShaderSourceComp* comp = data;
  switch (comp->type) {
  case AssetShaderSource_Repository:
    asset_repo_source_close(comp->srcRepo);
    break;
  case AssetShaderSource_Memory:
    alloc_free(g_allocHeap, comp->srcMem);
    break;
  }
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
  data_reg_field_t(g_dataReg, AssetShaderComp, inputMask, data_prim_t(u16));
  data_reg_field_t(g_dataReg, AssetShaderComp, outputMask, data_prim_t(u16));
  data_reg_field_t(g_dataReg, AssetShaderComp, entryPoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetShaderComp, resources, t_AssetShaderRes, .container = DataContainer_DataArray);
  data_reg_field_t(g_dataReg, AssetShaderComp, specs, t_AssetShaderSpec, .container = DataContainer_DataArray);
  data_reg_field_t(g_dataReg, AssetShaderComp, data, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetShaderDataDef = data_meta_t(t_AssetShaderComp);
}
