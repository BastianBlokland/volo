#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "geo_matrix.h"

#include "data_internal.h"
#include "repo_internal.h"

DataMeta g_assetMeshMeta;
DataMeta g_assetMeshSkeletonMeta;

ecs_comp_define_public(AssetMeshComp);
ecs_comp_define_public(AssetMeshSkeletonComp);

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
  }
}

ecs_module_init(asset_mesh_module) {
  ecs_register_comp(AssetMeshComp, .destructor = ecs_destruct_mesh_comp);
  ecs_register_comp(AssetMeshSkeletonComp, .destructor = ecs_destruct_mesh_skeleton_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadMeshAssetSys, ecs_view_id(UnloadView));
}

static DataType asset_mesh_index_type(void) {
  switch (sizeof(AssetMeshIndex)) {
  case sizeof(u16):
    return data_prim_t(u16);
  case sizeof(u32):
    return data_prim_t(u32);
  default:
    diag_crash();
  }
}

void asset_data_init_mesh(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetMeshVertex);
  data_reg_field_t(g_dataReg, AssetMeshVertex, position, g_assetGeoVec3Type);
  data_reg_field_t(g_dataReg, AssetMeshVertex, normal, g_assetGeoVec3Type);
  data_reg_field_t(g_dataReg, AssetMeshVertex, tangent, g_assetGeoVec4Type);
  data_reg_field_t(g_dataReg, AssetMeshVertex, texcoord, g_assetGeoVec2Type);

  data_reg_struct_t(g_dataReg, AssetMeshSkin);
  data_reg_field_t(g_dataReg, AssetMeshSkin, joints, data_prim_t(u8), .container = DataContainer_InlineArray, .fixedCount = 4);
  data_reg_field_t(g_dataReg, AssetMeshSkin, weights, g_assetGeoVec4Type);

  data_reg_struct_t(g_dataReg, AssetMeshComp);
  data_reg_field_t(g_dataReg, AssetMeshComp, vertices, t_AssetMeshVertex, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetMeshComp, skins, t_AssetMeshSkin, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetMeshComp, indices, asset_mesh_index_type(), .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetMeshComp, positionBounds, g_assetGeoBoxType);
  data_reg_field_t(g_dataReg, AssetMeshComp, positionRawBounds, g_assetGeoBoxType);
  data_reg_field_t(g_dataReg, AssetMeshComp, texcoordBounds, g_assetGeoBoxType);

  data_reg_enum_t(g_dataReg, AssetMeshAnimTarget);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Translation);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Rotation);
  data_reg_const_t(g_dataReg, AssetMeshAnimTarget, Scale);

  data_reg_struct_t(g_dataReg, AssetMeshAnimChannel);
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, frameCount, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, timeData, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshAnimChannel, valueData, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetMeshAnim);
  data_reg_field_t(g_dataReg, AssetMeshAnim, name, data_prim_t(StringHash));
  data_reg_field_t(g_dataReg, AssetMeshAnim, duration, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetMeshAnim, joints, t_AssetMeshAnimChannel, .container = DataContainer_InlineArray, .fixedCount = asset_mesh_joints_max * AssetMeshAnimTarget_Count);

  data_reg_struct_t(g_dataReg, AssetMeshSkeletonComp);
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, anims, t_AssetMeshAnim, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, bindPoseInvMats, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, defaultPose, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, rootTransform, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, parentIndices, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, skinCounts, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, jointNames, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, jointCount, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetMeshSkeletonComp, data, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetMeshMeta         = data_meta_t(t_AssetMeshComp);
  g_assetMeshSkeletonMeta = data_meta_t(t_AssetMeshSkeletonComp);
}
