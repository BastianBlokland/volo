#include "asset_terrain.h"
#include "core_alloc.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define terrain_max_size 1500
#define terrain_max_height 50.0f

DataMeta g_assetTerrainDefMeta;

ecs_comp_define_public(AssetTerrainComp);
ecs_comp_define(AssetTerrainInitComp);

static void ecs_destruct_terrain_comp(void* data) {
  AssetTerrainComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetTerrainDefMeta, mem_create(comp, sizeof(AssetTerrainComp)));
}

static void
terrain_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const String msg) {
  log_e(
      "Failed to parse terrain",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(msg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(InitView) {
  ecs_access_with(AssetTerrainInitComp);
  ecs_access_write(AssetTerrainComp);
  ecs_access_read(AssetComp);
}

ecs_view_define(UnloadView) {
  ecs_access_read(AssetTerrainComp);
  ecs_access_without(AssetTerrainInitComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Initialize terrain-assets.
 */
ecs_system_define(InitTerrainAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity      = ecs_view_entity(itr);
    const String      id          = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetTerrainComp* terrainComp = ecs_view_write_t(itr, AssetTerrainComp);
    const Mem         terrainMem  = mem_create(terrainComp, sizeof(AssetTerrainComp));

    if (!asset_data_patch_refs(world, manager, g_assetTerrainDefMeta, terrainMem)) {
      terrain_load_fail(world, entity, id, string_lit("Unable to resolve asset-reference"));
      goto Error;
    }
    if (!terrainComp->size || terrainComp->size > terrain_max_size) {
      terrain_load_fail(world, entity, id, string_lit("Invalid terrain size"));
      goto Error;
    }
    if (!terrainComp->playSize || terrainComp->playSize > terrainComp->size) {
      terrain_load_fail(world, entity, id, string_lit("Invalid terrain play size"));
      goto Error;
    }
    if (terrainComp->playSize % 2) {
      terrain_load_fail(
          world, entity, id, string_lit("Terrain play size has to be divisible by two"));
      goto Error;
    }
    if (terrainComp->heightMax < 0.0f || terrainComp->heightMax > terrain_max_height) {
      terrain_load_fail(world, entity, id, string_lit("Invalid terrain maximum height"));
      goto Error;
    }

    ecs_world_remove_t(world, entity, AssetTerrainInitComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    continue;

  Error:
    // NOTE: 'AssetTerrainComp' will be cleaned up by 'UnloadTerrainAssetSys'.
    ecs_world_remove_t(world, entity, AssetTerrainInitComp);
  }
}

/**
 * Remove any terrain-asset components for unloaded assets.
 */
ecs_system_define(UnloadTerrainAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetTerrainComp);
  }
}

ecs_module_init(asset_terrain_module) {
  ecs_register_comp(AssetTerrainComp, .destructor = ecs_destruct_terrain_comp);
  ecs_register_comp_empty(AssetTerrainInitComp);

  ecs_register_view(ManagerView);
  ecs_register_view(InitView);
  ecs_register_view(UnloadView);

  ecs_register_system(InitTerrainAssetSys, ecs_view_id(ManagerView), ecs_view_id(InitView));
  ecs_register_system(UnloadTerrainAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_terrain(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetTerrainComp);
  data_reg_field_t(g_dataReg, AssetTerrainComp, graphic, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetTerrainComp, heightmap, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetTerrainComp, size, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetTerrainComp, playSize, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetTerrainComp, heightMax, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetTerrainComp, minimapColorLow, g_assetGeoColor3NormType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetTerrainComp, minimapColorHigh, g_assetGeoColor3NormType, .flags = DataFlags_Opt);
  // clang-format on

  g_assetTerrainDefMeta = data_meta_t(t_AssetTerrainComp);
}

void asset_load_terrain(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  AssetTerrainComp* terrainComp = ecs_world_add_t(world, entity, AssetTerrainComp);
  const Mem         terrainMem  = mem_create(terrainComp, sizeof(AssetTerrainComp));

  DataReadResult result;
  if (src->format == AssetFormat_TerrainBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetTerrainDefMeta, terrainMem, &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetTerrainDefMeta, terrainMem, &result);
  }
  if (result.error) {
    terrain_load_fail(world, entity, id, result.errorMsg);
    goto Ret;
    // NOTE: 'AssetTerrainComp' will be cleaned up by 'UnloadTerrainAssetSys'.
  }

  if (src->format != AssetFormat_TerrainBin) {
    asset_cache(world, entity, g_assetTerrainDefMeta, terrainMem);
  }

  ecs_world_add_empty_t(world, entity, AssetTerrainInitComp);

Ret:
  asset_repo_source_close(src);
}
