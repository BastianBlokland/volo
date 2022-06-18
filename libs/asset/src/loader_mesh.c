#include "asset_mesh.h"
#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "geo_matrix.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetMeshComp);
ecs_comp_define_public(AssetMeshSkeletonComp);

static void ecs_destruct_mesh_comp(void* data) {
  AssetMeshComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->vertexData, comp->vertexCount);
  if (comp->skinData) {
    alloc_free_array_t(g_alloc_heap, comp->skinData, comp->vertexCount);
  }
  alloc_free_array_t(g_alloc_heap, comp->indexData, comp->indexCount);
}

static void ecs_destruct_mesh_skeleton_comp(void* data) {
  AssetMeshSkeletonComp* comp = data;

  for (u32 i = 0; i != comp->jointCount; ++i) {
    string_free(g_alloc_heap, comp->joints[i].name);
  }
  alloc_free_array_t(g_alloc_heap, comp->joints, comp->jointCount);
  alloc_free_array_t(g_alloc_heap, comp->childIndices, comp->jointCount);
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

const AssetMeshJoint*
asset_mesh_joints_create(Allocator* alloc, const AssetMeshSkeletonComp* skeleton) {
  AssetMeshJoint* res = alloc_array_t(alloc, AssetMeshJoint, skeleton->jointCount);
  for (u32 jointIndex = 0; jointIndex != skeleton->jointCount; ++jointIndex) {
    res[jointIndex] = (AssetMeshJoint){
        .invBindTransform = skeleton->joints[jointIndex].invBindTransform,
        .childIndex       = skeleton->joints[jointIndex].childIndex,
        .childCount       = skeleton->joints[jointIndex].childCount,
        .name             = string_dup(alloc, skeleton->joints[jointIndex].name),
    };
  }
  return res;
}

const u32*
asset_mesh_child_indices_create(Allocator* alloc, const AssetMeshSkeletonComp* skeleton) {
  const usize size   = sizeof(u32) * skeleton->jointCount;
  const Mem   orgMem = mem_create(skeleton->childIndices, size);
  return alloc_dup(alloc, orgMem, alignof(u32)).ptr;
}
