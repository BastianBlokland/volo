#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * GLTF (GL Transmission Format) 2.0.
 * Format specification: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html
 */

ecs_module_init(asset_gltf_module) {}

void asset_load_gltf(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)world;
  (void)id;
  (void)entity;
  (void)src;
}
