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
  alloc_free_array_t(g_alloc_heap, comp->joints, comp->jointCount);
  alloc_free_array_t(g_alloc_heap, comp->anims, comp->animCount);
  alloc_free(g_alloc_heap, comp->animData);
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
