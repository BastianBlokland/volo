#include "asset_material.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetMaterialComp);

static void ecs_destruct_material_comp(void* data) {
  AssetMaterialComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->shaders.assets, comp->shaders.count);
  alloc_free_array_t(g_alloc_heap, comp->samplers.values, comp->samplers.count);
}

typedef enum {
  MatError_None = 0,
  MatError_MalformedJson,
  MatError_MalformedMaterialObject,
  MatError_MalformedShaderReference,

  MatError_Count,
} MatError;

// static String mat_error_str(MatError res) {
//   static const String msgs[] = {
//       string_static("None"),
//       string_static("Malformed Json"),
//       string_static("Malformed Material object"),
//       string_static("Malformed Shader reference"),
//   };
//   ASSERT(array_elems(msgs) == MatError_Count, "Incorrect number of material-error messages");
//   return msgs[res];
// }

// NORETURN static void mat_report_error_msg(const MatError err, const String message) {
//   (void)err;
//   diag_crash_msg("Failed to parse material, error: {}", fmt_text(message));
// }

// NORETURN static void mat_report_error(const MatError err) {
//   mat_report_error_msg(err, mat_error_str(err));
// }

// ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); };

// ecs_view_define(BeginLoadView) {
//   ecs_access_with(AssetMaterialLoadingComp);
//   ecs_access_without(AssetMaterialComp);
// };

// /**
//  * Create and initialize material components for loading assets.
//  */
// ecs_system_define(LoadMaterialAssetSys) {
//   static const MatReader readers[] = {
//       mat_read_shaders,
//   };
//   AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
//   if (!manager) {
//     return;
//   }
//   EcsView* beginLoadView = ecs_world_view_t(world, BeginLoadView);
//   for (EcsIterator* itr = ecs_view_itr(beginLoadView); ecs_view_walk(itr);) {
//     const EcsEntityId               entity  = ecs_view_entity(itr);
//     const AssetMaterialLoadingComp* loading = ecs_view_read_t(itr, AssetMaterialLoadingComp);

//     AssetMaterialComp* material = ecs_world_add_t(world, entity, AssetMaterialComp);
//     array_for_t(readers, MatReader, reader, {
//       MatError error;
//       (*reader)(world, manager, loading, material, &error);
//       if (error) {
//         mat_report_error(error);
//       }
//     });
//   }
// }

ecs_view_define(UnloadView) {
  ecs_access_with(AssetMaterialComp);
  ecs_access_without(AssetLoadedComp);
};

/**
 * Remove any material-asset components for unloaded assets.
 */
ecs_system_define(UnloadMaterialAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetMaterialComp);
  }
}

ecs_module_init(asset_material_module) {
  ecs_register_comp(AssetMaterialComp, .destructor = ecs_destruct_material_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadMaterialAssetSys, ecs_view_id(UnloadView));
}

void asset_load_mat(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  (void)world;
  (void)assetEntity;
  (void)src;
  // ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
}
