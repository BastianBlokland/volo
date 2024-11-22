#include "core_alloc.h"
#include "core_diag.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "log_logger.h"

#include "data_internal.h"
#include "import_internal.h"
#include "loader_mesh_internal.h"
#include "repo_internal.h"

DataMeta g_assetMeshBundleMeta;
DataMeta g_assetMeshMeta;
DataMeta g_assetMeshSkeletonMeta;

ecs_comp_define_public(AssetMeshComp);
ecs_comp_define_public(AssetMeshSkeletonComp);
ecs_comp_define_public(AssetMeshSourceComp);

static void ecs_destruct_mesh_comp(void* data) {
  AssetMeshComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetMeshMeta, mem_create(comp, sizeof(AssetMeshComp)));
}

static void ecs_destruct_mesh_skeleton_comp(void* data) {
  AssetMeshSkeletonComp* comp = data;
  data_destroy(
      g_dataReg,
      g_allocHeap,
      g_assetMeshSkeletonMeta,
      mem_create(comp, sizeof(AssetMeshSkeletonComp)));
}

static void ecs_destruct_mesh_source_comp(void* data) {
  AssetMeshSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetMeshComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any mesh-asset components for unloaded assets.
 */
ecs_system_define(UnloadMeshAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetMeshComp);
    ecs_utils_maybe_remove_t(world, entity, AssetMeshSkeletonComp);
    ecs_utils_maybe_remove_t(world, entity, AssetMeshSourceComp);
  }
}

ecs_module_init(asset_mesh_module) {
  ecs_register_comp(AssetMeshComp, .destructor = ecs_destruct_mesh_comp);
  ecs_register_comp(AssetMeshSkeletonComp, .destructor = ecs_destruct_mesh_skeleton_comp);
  ecs_register_comp(AssetMeshSourceComp, .destructor = ecs_destruct_mesh_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadMeshAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_mesh(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetMeshComp);
  data_reg_field_t(g_dataReg, AssetMeshComp, vertexCount, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshComp, indexCount, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshComp, vertexData, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  data_reg_field_t(g_dataReg, AssetMeshComp, indexData, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  data_reg_field_t(g_dataReg, AssetMeshComp, bounds, g_assetGeoBoxType);

  data_reg_enum_t(g_dataReg, AssetMeshAnimTarget);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Translation);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Rotation);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Scale);

  data_reg_struct_t(g_dataReg, AssetMeshAnimChannel);
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, frameCount, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, timeData, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, valueData, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetMeshAnim);
  data_reg_field_t(g_dataReg, AssetMeshAnim, name, data_prim_t(String), .flags = DataFlags_Intern);
  data_reg_field_t(g_dataReg, AssetMeshAnim, duration, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetMeshAnim, time, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetMeshAnim, speed, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetMeshAnim, weight, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetMeshAnim, joints, t_AssetMeshAnimChannel, .container = DataContainer_InlineArray, .fixedCount = asset_mesh_joints_max * AssetMeshAnimTarget_Count);

  data_reg_struct_t(g_dataReg, AssetMeshSkeletonComp);
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, anims, t_AssetMeshAnim, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, bindMatInv, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, defaultPose, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, rootTransform, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, parentIndices, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, skinCounts, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, boundingRadius, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, jointNameHashes, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, jointNames, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, jointCount, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, data, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);

  data_reg_struct_t(g_dataReg, AssetMeshBundle);
  data_reg_field_t(g_dataReg, AssetMeshBundle, mesh, t_AssetMeshComp);
  data_reg_field_t(g_dataReg, AssetMeshBundle, skeleton, t_AssetMeshSkeletonComp, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
  // clang-format on

  g_assetMeshBundleMeta   = data_meta_t(t_AssetMeshBundle);
  g_assetMeshMeta         = data_meta_t(t_AssetMeshComp);
  g_assetMeshSkeletonMeta = data_meta_t(t_AssetMeshSkeletonComp);
}

void asset_load_mesh_bin(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetMeshBundle bundle;
  DataReadResult  result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetMeshBundleMeta, mem_var(bundle), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary mesh",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetMeshComp) = bundle.mesh;
  if (bundle.skeleton) {
    *ecs_world_add_t(world, entity, AssetMeshSkeletonComp) = *bundle.skeleton;
    alloc_free_t(g_allocHeap, bundle.skeleton);
  }

  ecs_world_add_t(world, entity, AssetMeshSourceComp, .src = src);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}
