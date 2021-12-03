#include "asset_mesh.h"
#include "ecs_world.h"

#include "repo_internal.h"

/**
 * Wavefront Obj.
 * Polygonal faces are supported (no curves or lines), materials are also ignored at this time.
 * Format specification: http://www.martinreddy.net/gfx/3d/OBJ.spec
 * Faces are assumed to be contex and a triangulated using a simple triangle fan.
 */

void asset_load_obj(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  (void)world;
  (void)assetEntity;
  (void)src;
}
