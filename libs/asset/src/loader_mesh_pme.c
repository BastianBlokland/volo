#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * ProceduralMEsh - Procedurally generated mesh.
 */

#define asset_pme_max_verts 512

// typedef enum {
//   PmeError_None = 0,

//   PmeError_Count,
// } PmeError;

// static String pme_error_str(const PmeError err) {
//   static const String g_msgs[] = {
//       string_static("None"),
//   };
//   ASSERT(array_elems(g_msgs) == PmeError_Count, "Incorrect number of pme-error messages");
//   return g_msgs[err];
// }

void asset_load_pme(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  // String            errMsg;
  AssetMeshBuilder* builder = asset_mesh_builder_create(g_alloc_heap, asset_pme_max_verts);

  *ecs_world_add_t(world, entity, AssetMeshComp) = asset_mesh_create(builder);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_repo_source_close(src);
  goto Done;

  // Error:
  //   log_e("Failed to load pme mesh", log_param("error", fmt_text(errMsg)));
  //   ecs_world_add_empty_t(world, entity, AssetFailedComp);

Done:
  asset_mesh_builder_destroy(builder);
  asset_repo_source_close(src);
}
